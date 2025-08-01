//===-- StackFrameList.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/StackFrameList.h"
#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/SourceManager.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/StackFrameRecognizer.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/Unwind.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "llvm/ADT/SmallPtrSet.h"

#include <memory>

//#define DEBUG_STACK_FRAMES 1

using namespace lldb;
using namespace lldb_private;

// StackFrameList constructor
StackFrameList::StackFrameList(Thread &thread,
                               const lldb::StackFrameListSP &prev_frames_sp,
                               bool show_inline_frames)
    : m_thread(thread), m_prev_frames_sp(prev_frames_sp), m_frames(),
      m_selected_frame_idx(), m_concrete_frames_fetched(0),
      m_current_inlined_depth(UINT32_MAX),
      m_current_inlined_pc(LLDB_INVALID_ADDRESS),
      m_show_inlined_frames(show_inline_frames) {
  if (prev_frames_sp) {
    m_current_inlined_depth = prev_frames_sp->m_current_inlined_depth;
    m_current_inlined_pc = prev_frames_sp->m_current_inlined_pc;
  }
}

StackFrameList::~StackFrameList() {
  // Call clear since this takes a lock and clears the stack frame list in case
  // another thread is currently using this stack frame list
  Clear();
}

void StackFrameList::CalculateCurrentInlinedDepth() {
  uint32_t cur_inlined_depth = GetCurrentInlinedDepth();
  if (cur_inlined_depth == UINT32_MAX) {
    ResetCurrentInlinedDepth();
  }
}

uint32_t StackFrameList::GetCurrentInlinedDepth() {
  std::lock_guard<std::mutex> guard(m_inlined_depth_mutex);
  if (m_show_inlined_frames && m_current_inlined_pc != LLDB_INVALID_ADDRESS) {
    lldb::addr_t cur_pc = m_thread.GetRegisterContext()->GetPC();
    if (cur_pc != m_current_inlined_pc) {
      m_current_inlined_pc = LLDB_INVALID_ADDRESS;
      m_current_inlined_depth = UINT32_MAX;
      Log *log = GetLog(LLDBLog::Step);
      if (log && log->GetVerbose())
        LLDB_LOGF(
            log,
            "GetCurrentInlinedDepth: invalidating current inlined depth.\n");
    }
    return m_current_inlined_depth;
  } else {
    return UINT32_MAX;
  }
}

void StackFrameList::ResetCurrentInlinedDepth() {
  if (!m_show_inlined_frames)
    return;

  StopInfoSP stop_info_sp = m_thread.GetStopInfo();
  if (!stop_info_sp)
    return;

  bool inlined = true;
  auto inline_depth = stop_info_sp->GetSuggestedStackFrameIndex(inlined);
  // We're only adjusting the inlined stack here.
  Log *log = GetLog(LLDBLog::Step);
  if (inline_depth) {
    std::lock_guard<std::mutex> guard(m_inlined_depth_mutex);
    m_current_inlined_depth = *inline_depth;
    m_current_inlined_pc = m_thread.GetRegisterContext()->GetPC();

    if (log && log->GetVerbose())
      LLDB_LOGF(log,
                "ResetCurrentInlinedDepth: setting inlined "
                "depth: %d 0x%" PRIx64 ".\n",
                m_current_inlined_depth, m_current_inlined_pc);
  } else {
    std::lock_guard<std::mutex> guard(m_inlined_depth_mutex);
    m_current_inlined_pc = LLDB_INVALID_ADDRESS;
    m_current_inlined_depth = UINT32_MAX;
    if (log && log->GetVerbose())
      LLDB_LOGF(
          log,
          "ResetCurrentInlinedDepth: Invalidating current inlined depth.\n");
  }
}

bool StackFrameList::DecrementCurrentInlinedDepth() {
  if (m_show_inlined_frames) {
    uint32_t current_inlined_depth = GetCurrentInlinedDepth();
    if (current_inlined_depth != UINT32_MAX) {
      if (current_inlined_depth > 0) {
        std::lock_guard<std::mutex> guard(m_inlined_depth_mutex);
        m_current_inlined_depth--;
        return true;
      }
    }
  }
  return false;
}

void StackFrameList::SetCurrentInlinedDepth(uint32_t new_depth) {
  std::lock_guard<std::mutex> guard(m_inlined_depth_mutex);
  m_current_inlined_depth = new_depth;
  if (new_depth == UINT32_MAX)
    m_current_inlined_pc = LLDB_INVALID_ADDRESS;
  else
    m_current_inlined_pc = m_thread.GetRegisterContext()->GetPC();
}

bool StackFrameList::WereAllFramesFetched() const {
  std::shared_lock<std::shared_mutex> guard(m_list_mutex);
  return GetAllFramesFetched();
}

/// A sequence of calls that comprise some portion of a backtrace. Each frame
/// is represented as a pair of a callee (Function *) and an address within the
/// callee.
struct CallDescriptor {
  Function *func;
  CallEdge::AddrType address_type = CallEdge::AddrType::Call;
  addr_t address = LLDB_INVALID_ADDRESS;
};
using CallSequence = std::vector<CallDescriptor>;

/// Find the unique path through the call graph from \p begin (with return PC
/// \p return_pc) to \p end. On success this path is stored into \p path, and
/// on failure \p path is unchanged.
/// This function doesn't currently access StackFrameLists at all, it only looks
/// at the frame set in the ExecutionContext it passes around.
static void FindInterveningFrames(Function &begin, Function &end,
                                  ExecutionContext &exe_ctx, Target &target,
                                  addr_t return_pc, CallSequence &path,
                                  ModuleList &images, Log *log) {
  LLDB_LOG(log, "Finding frames between {0} and {1}, retn-pc={2:x}",
           begin.GetDisplayName(), end.GetDisplayName(), return_pc);

  // Find a non-tail calling edge with the correct return PC.
  if (log)
    for (const auto &edge : begin.GetCallEdges())
      LLDB_LOG(log, "FindInterveningFrames: found call with retn-PC = {0:x}",
               edge->GetReturnPCAddress(begin, target));
  CallEdge *first_edge = begin.GetCallEdgeForReturnAddress(return_pc, target);
  if (!first_edge) {
    LLDB_LOG(log, "No call edge outgoing from {0} with retn-PC == {1:x}",
             begin.GetDisplayName(), return_pc);
    return;
  }

  // The first callee may not be resolved, or there may be nothing to fill in.
  Function *first_callee = first_edge->GetCallee(images, exe_ctx);
  if (!first_callee) {
    LLDB_LOG(log, "Could not resolve callee");
    return;
  }
  if (first_callee == &end) {
    LLDB_LOG(log, "Not searching further, first callee is {0} (retn-PC: {1:x})",
             end.GetDisplayName(), return_pc);
    return;
  }

  // Run DFS on the tail-calling edges out of the first callee to find \p end.
  // Fully explore the set of functions reachable from the first edge via tail
  // calls in order to detect ambiguous executions.
  struct DFS {
    CallSequence active_path = {};
    CallSequence solution_path = {};
    llvm::SmallPtrSet<Function *, 2> visited_nodes = {};
    bool ambiguous = false;
    Function *end;
    ModuleList &images;
    Target &target;
    ExecutionContext &context;

    DFS(Function *end, ModuleList &images, Target &target,
        ExecutionContext &context)
        : end(end), images(images), target(target), context(context) {}

    void search(CallEdge &first_edge, Function &first_callee,
                CallSequence &path) {
      dfs(first_edge, first_callee);
      if (!ambiguous)
        path = std::move(solution_path);
    }

    void dfs(CallEdge &current_edge, Function &callee) {
      // Found a path to the target function.
      if (&callee == end) {
        if (solution_path.empty())
          solution_path = active_path;
        else
          ambiguous = true;
        return;
      }

      // Terminate the search if tail recursion is found, or more generally if
      // there's more than one way to reach a target. This errs on the side of
      // caution: it conservatively stops searching when some solutions are
      // still possible to save time in the average case.
      if (!visited_nodes.insert(&callee).second) {
        ambiguous = true;
        return;
      }

      // Search the calls made from this callee.
      active_path.push_back(CallDescriptor{&callee});
      for (const auto &edge : callee.GetTailCallingEdges()) {
        Function *next_callee = edge->GetCallee(images, context);
        if (!next_callee)
          continue;

        std::tie(active_path.back().address_type, active_path.back().address) =
            edge->GetCallerAddress(callee, target);

        dfs(*edge, *next_callee);
        if (ambiguous)
          return;
      }
      active_path.pop_back();
    }
  };

  DFS(&end, images, target, exe_ctx).search(*first_edge, *first_callee, path);
}

/// Given that \p next_frame will be appended to the frame list, synthesize
/// tail call frames between the current end of the list and \p next_frame.
/// If any frames are added, adjust the frame index of \p next_frame.
///
///   --------------
///   |    ...     | <- Completed frames.
///   --------------
///   | prev_frame |
///   --------------
///   |    ...     | <- Artificial frames inserted here.
///   --------------
///   | next_frame |
///   --------------
///   |    ...     | <- Not-yet-visited frames.
///   --------------
void StackFrameList::SynthesizeTailCallFrames(StackFrame &next_frame) {
  // Cannot synthesize tail call frames when the stack is empty (there is no
  // "previous" frame).
  if (m_frames.empty())
    return;

  TargetSP target_sp = next_frame.CalculateTarget();
  if (!target_sp)
    return;

  lldb::RegisterContextSP next_reg_ctx_sp = next_frame.GetRegisterContext();
  if (!next_reg_ctx_sp)
    return;

  Log *log = GetLog(LLDBLog::Step);

  StackFrame &prev_frame = *m_frames.back().get();

  // Find the functions prev_frame and next_frame are stopped in. The function
  // objects are needed to search the lazy call graph for intervening frames.
  Function *prev_func =
      prev_frame.GetSymbolContext(eSymbolContextFunction).function;
  if (!prev_func) {
    LLDB_LOG(log, "SynthesizeTailCallFrames: can't find previous function");
    return;
  }
  Function *next_func =
      next_frame.GetSymbolContext(eSymbolContextFunction).function;
  if (!next_func) {
    LLDB_LOG(log, "SynthesizeTailCallFrames: can't find next function");
    return;
  }

  // Try to find the unique sequence of (tail) calls which led from next_frame
  // to prev_frame.
  CallSequence path;
  addr_t return_pc = next_reg_ctx_sp->GetPC();
  Target &target = *target_sp.get();
  ModuleList &images = next_frame.CalculateTarget()->GetImages();
  ExecutionContext exe_ctx(target_sp, /*get_process=*/true);
  exe_ctx.SetFramePtr(&next_frame);
  FindInterveningFrames(*next_func, *prev_func, exe_ctx, target, return_pc,
                        path, images, log);

  // Push synthetic tail call frames.
  for (auto calleeInfo : llvm::reverse(path)) {
    Function *callee = calleeInfo.func;
    uint32_t frame_idx = m_frames.size();
    uint32_t concrete_frame_idx = next_frame.GetConcreteFrameIndex();
    addr_t cfa = LLDB_INVALID_ADDRESS;
    bool cfa_is_valid = false;
    addr_t pc = calleeInfo.address;
    // If the callee address refers to the call instruction, we do not want to
    // subtract 1 from this value.
    const bool behaves_like_zeroth_frame =
        calleeInfo.address_type == CallEdge::AddrType::Call;
    SymbolContext sc;
    callee->CalculateSymbolContext(&sc);
    auto synth_frame = std::make_shared<StackFrame>(
        m_thread.shared_from_this(), frame_idx, concrete_frame_idx, cfa,
        cfa_is_valid, pc, StackFrame::Kind::Artificial,
        behaves_like_zeroth_frame, &sc);
    m_frames.push_back(synth_frame);
    LLDB_LOG(log, "Pushed frame {0} at {1:x}", callee->GetDisplayName(), pc);
  }

  // If any frames were created, adjust next_frame's index.
  if (!path.empty())
    next_frame.SetFrameIndex(m_frames.size());
}

bool StackFrameList::GetFramesUpTo(uint32_t end_idx,
                                   InterruptionControl allow_interrupt) {
  // GetFramesUpTo is always called with the intent to add frames, so get the
  // writer lock:
  std::unique_lock<std::shared_mutex> guard(m_list_mutex);
  // Now that we have the lock, check to make sure someone didn't get there
  // ahead of us:
  if (m_frames.size() > end_idx || GetAllFramesFetched())
    return false;

  // Do not fetch frames for an invalid thread.
  bool was_interrupted = false;
  if (!m_thread.IsValid())
    return false;

  // lock the writer side of m_list_mutex as we're going to add frames here:
  if (!m_show_inlined_frames) {
    if (end_idx < m_concrete_frames_fetched)
      return false;
    // We're adding concrete frames now:
    // FIXME: This should also be interruptible:
    FetchOnlyConcreteFramesUpTo(end_idx);
    return false;
  }

  // We're adding concrete and inlined frames now:
  was_interrupted = FetchFramesUpTo(end_idx, allow_interrupt);

#if defined(DEBUG_STACK_FRAMES)
  s.PutCString("\n\nNew frames:\n");
  Dump(&s);
  s.EOL();
#endif
  return was_interrupted;
}

void StackFrameList::FetchOnlyConcreteFramesUpTo(uint32_t end_idx) {
  assert(m_thread.IsValid() && "Expected valid thread");
  assert(m_frames.size() <= end_idx && "Expected there to be frames to fill");

  Unwind &unwinder = m_thread.GetUnwinder();

  if (end_idx < m_concrete_frames_fetched)
    return;

  uint32_t num_frames = unwinder.GetFramesUpTo(end_idx);
  if (num_frames <= end_idx + 1) {
    // Done unwinding.
    m_concrete_frames_fetched = UINT32_MAX;
  }

  // Don't create the frames eagerly. Defer this work to GetFrameAtIndex,
  // which can lazily query the unwinder to create frames.
  m_frames.resize(num_frames);
}

bool StackFrameList::FetchFramesUpTo(uint32_t end_idx,
                                     InterruptionControl allow_interrupt) {
  Unwind &unwinder = m_thread.GetUnwinder();
  bool was_interrupted = false;

#if defined(DEBUG_STACK_FRAMES)
  StreamFile s(stdout, false);
#endif
  // If we are hiding some frames from the outside world, we need to add
  // those onto the total count of frames to fetch.  However, we don't need
  // to do that if end_idx is 0 since in that case we always get the first
  // concrete frame and all the inlined frames below it...  And of course, if
  // end_idx is UINT32_MAX that means get all, so just do that...

  uint32_t inlined_depth = 0;
  if (end_idx > 0 && end_idx != UINT32_MAX) {
    inlined_depth = GetCurrentInlinedDepth();
    if (inlined_depth != UINT32_MAX) {
      if (end_idx > 0)
        end_idx += inlined_depth;
    }
  }

  StackFrameSP unwind_frame_sp;
  Debugger &dbg = m_thread.GetProcess()->GetTarget().GetDebugger();
  do {
    uint32_t idx = m_concrete_frames_fetched++;
    lldb::addr_t pc = LLDB_INVALID_ADDRESS;
    lldb::addr_t cfa = LLDB_INVALID_ADDRESS;
    bool behaves_like_zeroth_frame = (idx == 0);
    if (idx == 0) {
      // We might have already created frame zero, only create it if we need
      // to.
      if (m_frames.empty()) {
        RegisterContextSP reg_ctx_sp(m_thread.GetRegisterContext());

        if (reg_ctx_sp) {
          const bool success = unwinder.GetFrameInfoAtIndex(
              idx, cfa, pc, behaves_like_zeroth_frame);
          // There shouldn't be any way not to get the frame info for frame
          // 0. But if the unwinder can't make one, lets make one by hand
          // with the SP as the CFA and see if that gets any further.
          if (!success) {
            cfa = reg_ctx_sp->GetSP();
            pc = reg_ctx_sp->GetPC();
          }

          unwind_frame_sp = std::make_shared<StackFrame>(
              m_thread.shared_from_this(), m_frames.size(), idx, reg_ctx_sp,
              cfa, pc, behaves_like_zeroth_frame, nullptr);
          m_frames.push_back(unwind_frame_sp);
        }
      } else {
        unwind_frame_sp = m_frames.front();
        cfa = unwind_frame_sp->m_id.GetCallFrameAddress();
      }
    } else {
      // Check for interruption when building the frames.
      // Do the check in idx > 0 so that we'll always create a 0th frame.
      if (allow_interrupt &&
          INTERRUPT_REQUESTED(dbg, "Interrupted having fetched {0} frames",
                              m_frames.size())) {
        was_interrupted = true;
        break;
      }

      const bool success =
          unwinder.GetFrameInfoAtIndex(idx, cfa, pc, behaves_like_zeroth_frame);
      if (!success) {
        // We've gotten to the end of the stack.
        SetAllFramesFetched();
        break;
      }
      const bool cfa_is_valid = true;
      unwind_frame_sp = std::make_shared<StackFrame>(
          m_thread.shared_from_this(), m_frames.size(), idx, cfa, cfa_is_valid,
          pc, StackFrame::Kind::Regular, behaves_like_zeroth_frame, nullptr);

      // Create synthetic tail call frames between the previous frame and the
      // newly-found frame. The new frame's index may change after this call,
      // although its concrete index will stay the same.
      SynthesizeTailCallFrames(*unwind_frame_sp.get());

      m_frames.push_back(unwind_frame_sp);
    }

    assert(unwind_frame_sp);
    SymbolContext unwind_sc = unwind_frame_sp->GetSymbolContext(
        eSymbolContextBlock | eSymbolContextFunction);
    Block *unwind_block = unwind_sc.block;
    TargetSP target_sp = m_thread.CalculateTarget();
    if (unwind_block) {
      Address curr_frame_address(
          unwind_frame_sp->GetFrameCodeAddressForSymbolication());

      SymbolContext next_frame_sc;
      Address next_frame_address;

      while (unwind_sc.GetParentOfInlinedScope(
          curr_frame_address, next_frame_sc, next_frame_address)) {
        next_frame_sc.line_entry.ApplyFileMappings(target_sp);
        behaves_like_zeroth_frame = false;
        StackFrameSP frame_sp(new StackFrame(
            m_thread.shared_from_this(), m_frames.size(), idx,
            unwind_frame_sp->GetRegisterContextSP(), cfa, next_frame_address,
            behaves_like_zeroth_frame, &next_frame_sc));

        m_frames.push_back(frame_sp);
        unwind_sc = next_frame_sc;
        curr_frame_address = next_frame_address;
      }
    }
  } while (m_frames.size() - 1 < end_idx);

  // Don't try to merge till you've calculated all the frames in this stack.
  if (GetAllFramesFetched() && m_prev_frames_sp) {
    StackFrameList *prev_frames = m_prev_frames_sp.get();
    StackFrameList *curr_frames = this;

#if defined(DEBUG_STACK_FRAMES)
    s.PutCString("\nprev_frames:\n");
    prev_frames->Dump(&s);
    s.PutCString("\ncurr_frames:\n");
    curr_frames->Dump(&s);
    s.EOL();
#endif
    size_t curr_frame_num, prev_frame_num;

    for (curr_frame_num = curr_frames->m_frames.size(),
        prev_frame_num = prev_frames->m_frames.size();
         curr_frame_num > 0 && prev_frame_num > 0;
         --curr_frame_num, --prev_frame_num) {
      const size_t curr_frame_idx = curr_frame_num - 1;
      const size_t prev_frame_idx = prev_frame_num - 1;
      StackFrameSP curr_frame_sp(curr_frames->m_frames[curr_frame_idx]);
      StackFrameSP prev_frame_sp(prev_frames->m_frames[prev_frame_idx]);

#if defined(DEBUG_STACK_FRAMES)
      s.Printf("\n\nCurr frame #%u ", curr_frame_idx);
      if (curr_frame_sp)
        curr_frame_sp->Dump(&s, true, false);
      else
        s.PutCString("NULL");
      s.Printf("\nPrev frame #%u ", prev_frame_idx);
      if (prev_frame_sp)
        prev_frame_sp->Dump(&s, true, false);
      else
        s.PutCString("NULL");
#endif

      StackFrame *curr_frame = curr_frame_sp.get();
      StackFrame *prev_frame = prev_frame_sp.get();

      if (curr_frame == nullptr || prev_frame == nullptr)
        break;

      // Check the stack ID to make sure they are equal.
      if (curr_frame->GetStackID() != prev_frame->GetStackID())
        break;

      prev_frame->UpdatePreviousFrameFromCurrentFrame(*curr_frame);
      // Now copy the fixed up previous frame into the current frames so the
      // pointer doesn't change.
      m_frames[curr_frame_idx] = prev_frame_sp;

#if defined(DEBUG_STACK_FRAMES)
      s.Printf("\n    Copying previous frame to current frame");
#endif
    }
    // We are done with the old stack frame list, we can release it now.
    m_prev_frames_sp.reset();
  }
  // Don't report interrupted if we happen to have gotten all the frames:
  if (!GetAllFramesFetched())
    return was_interrupted;
  return false;
}

uint32_t StackFrameList::GetNumFrames(bool can_create) {
  if (!WereAllFramesFetched() && can_create) {
    // Don't allow interrupt or we might not return the correct count
    GetFramesUpTo(UINT32_MAX, DoNotAllowInterruption);
  }
  uint32_t frame_idx;
  {
    std::shared_lock<std::shared_mutex> guard(m_list_mutex);
    frame_idx = GetVisibleStackFrameIndex(m_frames.size());
  }
  return frame_idx;
}

void StackFrameList::Dump(Stream *s) {
  if (s == nullptr)
    return;

  std::shared_lock<std::shared_mutex> guard(m_list_mutex);

  const_iterator pos, begin = m_frames.begin(), end = m_frames.end();
  for (pos = begin; pos != end; ++pos) {
    StackFrame *frame = (*pos).get();
    s->Printf("%p: ", static_cast<void *>(frame));
    if (frame) {
      frame->GetStackID().Dump(s);
      frame->DumpUsingSettingsFormat(s);
    } else
      s->Printf("frame #%u", (uint32_t)std::distance(begin, pos));
    s->EOL();
  }
  s->EOL();
}

StackFrameSP StackFrameList::GetFrameAtIndex(uint32_t idx) {
  StackFrameSP frame_sp;
  uint32_t original_idx = idx;

  // We're going to consult the m_frames.size, but if there are already
  // enough frames for our request we don't want to block other readers, so
  // first acquire the shared lock:
  { // Scope for shared lock:
    std::shared_lock<std::shared_mutex> guard(m_list_mutex);

    uint32_t inlined_depth = GetCurrentInlinedDepth();
    if (inlined_depth != UINT32_MAX)
      idx += inlined_depth;

    if (idx < m_frames.size())
      frame_sp = m_frames[idx];

    if (frame_sp)
      return frame_sp;
  } // End of reader lock scope

  // GetFramesUpTo will fill m_frames with as many frames as you asked for, if
  // there are that many.  If there weren't then you asked for too many frames.
  // GetFramesUpTo returns true if interrupted:
  if (GetFramesUpTo(idx, AllowInterruption)) {
    Log *log = GetLog(LLDBLog::Thread);
    LLDB_LOG(log, "GetFrameAtIndex was interrupted");
    return {};
  }

  { // Now we're accessing m_frames as a reader, so acquire the reader lock.
    std::shared_lock<std::shared_mutex> guard(m_list_mutex);
    if (idx < m_frames.size()) {
      frame_sp = m_frames[idx];
    } else if (original_idx == 0) {
      // There should ALWAYS be a frame at index 0.  If something went wrong
      // with the CurrentInlinedDepth such that there weren't as many frames as
      // we thought taking that into account, then reset the current inlined
      // depth and return the real zeroth frame.
      if (m_frames.empty()) {
        // Why do we have a thread with zero frames, that should not ever
        // happen...
        assert(!m_thread.IsValid() && "A valid thread has no frames.");
      } else {
        ResetCurrentInlinedDepth();
        frame_sp = m_frames[original_idx];
      }
    }
  } // End of reader lock scope

  return frame_sp;
}

StackFrameSP
StackFrameList::GetFrameWithConcreteFrameIndex(uint32_t unwind_idx) {
  // First try assuming the unwind index is the same as the frame index. The
  // unwind index is always greater than or equal to the frame index, so it is
  // a good place to start. If we have inlined frames we might have 5 concrete
  // frames (frame unwind indexes go from 0-4), but we might have 15 frames
  // after we make all the inlined frames. Most of the time the unwind frame
  // index (or the concrete frame index) is the same as the frame index.
  uint32_t frame_idx = unwind_idx;
  StackFrameSP frame_sp(GetFrameAtIndex(frame_idx));
  while (frame_sp) {
    if (frame_sp->GetFrameIndex() == unwind_idx)
      break;
    frame_sp = GetFrameAtIndex(++frame_idx);
  }
  return frame_sp;
}

static bool CompareStackID(const StackFrameSP &stack_sp,
                           const StackID &stack_id) {
  return stack_sp->GetStackID() < stack_id;
}

StackFrameSP StackFrameList::GetFrameWithStackID(const StackID &stack_id) {
  StackFrameSP frame_sp;

  if (stack_id.IsValid()) {
    uint32_t frame_idx = 0;
    {
      // First see if the frame is already realized.  This is the scope for
      // the shared mutex:
      std::shared_lock<std::shared_mutex> guard(m_list_mutex);
      // Do a binary search in case the stack frame is already in our cache
      collection::const_iterator pos =
          llvm::lower_bound(m_frames, stack_id, CompareStackID);
      if (pos != m_frames.end() && (*pos)->GetStackID() == stack_id)
        return *pos;
    }
    // If we needed to add more frames, we would get to here.
    do {
      frame_sp = GetFrameAtIndex(frame_idx);
      if (frame_sp && frame_sp->GetStackID() == stack_id)
        break;
      frame_idx++;
    } while (frame_sp);
  }
  return frame_sp;
}

bool StackFrameList::SetFrameAtIndex(uint32_t idx, StackFrameSP &frame_sp) {
  std::unique_lock<std::shared_mutex> guard(m_list_mutex);
  if (idx >= m_frames.size())
    m_frames.resize(idx + 1);
  // Make sure allocation succeeded by checking bounds again
  if (idx < m_frames.size()) {
    m_frames[idx] = frame_sp;
    return true;
  }
  return false; // resize failed, out of memory?
}

void StackFrameList::SelectMostRelevantFrame() {
  // Don't call into the frame recognizers on the private state thread as
  // they can cause code to run in the target, and that can cause deadlocks
  // when fetching stop events for the expression.
  if (m_thread.GetProcess()->CurrentThreadPosesAsPrivateStateThread())
    return;

  Log *log = GetLog(LLDBLog::Thread);

  // Only the top frame should be recognized.
  StackFrameSP frame_sp = GetFrameAtIndex(0);
  if (!frame_sp) {
    LLDB_LOG(log, "Failed to construct Frame #0");
    return;
  }

  RecognizedStackFrameSP recognized_frame_sp = frame_sp->GetRecognizedFrame();

  if (recognized_frame_sp) {
    if (StackFrameSP most_relevant_frame_sp =
            recognized_frame_sp->GetMostRelevantFrame()) {
      LLDB_LOG(log, "Found most relevant frame at index {0}",
               most_relevant_frame_sp->GetFrameIndex());
      SetSelectedFrame(most_relevant_frame_sp.get());
      return;
    }
  }
  LLDB_LOG(log, "Frame #0 not recognized");

  // If this thread has a non-trivial StopInfo, then let it suggest
  // a most relevant frame:
  StopInfoSP stop_info_sp = m_thread.GetStopInfo();
  uint32_t stack_idx = 0;
  bool found_relevant = false;
  if (stop_info_sp) {
    // Here we're only asking the stop info if it wants to adjust the real stack
    // index.  We have to ask about the m_inlined_stack_depth in
    // Thread::ShouldStop since the plans need to reason with that info.
    bool inlined = false;
    std::optional<uint32_t> stack_opt =
        stop_info_sp->GetSuggestedStackFrameIndex(inlined);
    if (stack_opt) {
      stack_idx = *stack_opt;
      found_relevant = true;
    }
  }

  frame_sp = GetFrameAtIndex(stack_idx);
  if (!frame_sp)
    LLDB_LOG(log, "Stop info suggested relevant frame {0} but it didn't exist",
             stack_idx);
  else if (found_relevant)
    LLDB_LOG(log, "Setting selected frame from stop info to {0}", stack_idx);
  // Note, we don't have to worry about "inlined" frames here, because we've
  // already calculated the inlined frame in Thread::ShouldStop, and
  // SetSelectedFrame will take care of that adjustment for us.
  SetSelectedFrame(frame_sp.get());

  if (!found_relevant)
    LLDB_LOG(log, "No relevant frame!");
}

uint32_t
StackFrameList::GetSelectedFrameIndex(SelectMostRelevant select_most_relevant) {
  std::lock_guard<std::recursive_mutex> guard(m_selected_frame_mutex);

  if (!m_selected_frame_idx && select_most_relevant)
    SelectMostRelevantFrame();
  if (!m_selected_frame_idx) {
    // If we aren't selecting the most relevant frame, and the selected frame
    // isn't set, then don't force a selection here, just return 0.
    if (!select_most_relevant)
      return 0;
    // If the inlined stack frame is set, then use that:
    m_selected_frame_idx = 0;
  }
  return *m_selected_frame_idx;
}

uint32_t StackFrameList::SetSelectedFrame(lldb_private::StackFrame *frame) {
  std::shared_lock<std::shared_mutex> guard(m_list_mutex);
  std::lock_guard<std::recursive_mutex> selected_frame_guard(
      m_selected_frame_mutex);

  const_iterator pos;
  const_iterator begin = m_frames.begin();
  const_iterator end = m_frames.end();
  m_selected_frame_idx = 0;

  for (pos = begin; pos != end; ++pos) {
    if (pos->get() == frame) {
      m_selected_frame_idx = std::distance(begin, pos);
      uint32_t inlined_depth = GetCurrentInlinedDepth();
      if (inlined_depth != UINT32_MAX)
        m_selected_frame_idx = *m_selected_frame_idx - inlined_depth;
      break;
    }
  }
  SetDefaultFileAndLineToSelectedFrame();
  return *m_selected_frame_idx;
}

bool StackFrameList::SetSelectedFrameByIndex(uint32_t idx) {
  StackFrameSP frame_sp(GetFrameAtIndex(idx));
  if (frame_sp) {
    SetSelectedFrame(frame_sp.get());
    return true;
  } else
    return false;
}

void StackFrameList::SetDefaultFileAndLineToSelectedFrame() {
  if (m_thread.GetID() ==
      m_thread.GetProcess()->GetThreadList().GetSelectedThread()->GetID()) {
    StackFrameSP frame_sp(
        GetFrameAtIndex(GetSelectedFrameIndex(DoNoSelectMostRelevantFrame)));
    if (frame_sp) {
      SymbolContext sc = frame_sp->GetSymbolContext(eSymbolContextLineEntry);
      if (sc.line_entry.GetFile())
        m_thread.CalculateTarget()->GetSourceManager().SetDefaultFileAndLine(
            sc.line_entry.file_sp, sc.line_entry.line);
    }
  }
}

// The thread has been run, reset the number stack frames to zero so we can
// determine how many frames we have lazily.
// Note, we don't actually re-use StackFrameLists, we always make a new
// StackFrameList every time we stop, and then copy frame information frame
// by frame from the old to the new StackFrameList.  So the comment above,
// does not describe how StackFrameLists are currently used.
// Clear is currently only used to clear the list in the destructor.
void StackFrameList::Clear() {
  std::unique_lock<std::shared_mutex> guard(m_list_mutex);
  m_frames.clear();
  m_concrete_frames_fetched = 0;
  std::lock_guard<std::recursive_mutex> selected_frame_guard(
      m_selected_frame_mutex);
  m_selected_frame_idx.reset();
}

lldb::StackFrameSP
StackFrameList::GetStackFrameSPForStackFramePtr(StackFrame *stack_frame_ptr) {
  std::shared_lock<std::shared_mutex> guard(m_list_mutex);
  const_iterator pos;
  const_iterator begin = m_frames.begin();
  const_iterator end = m_frames.end();
  lldb::StackFrameSP ret_sp;

  for (pos = begin; pos != end; ++pos) {
    if (pos->get() == stack_frame_ptr) {
      ret_sp = (*pos);
      break;
    }
  }
  return ret_sp;
}

size_t StackFrameList::GetStatus(Stream &strm, uint32_t first_frame,
                                 uint32_t num_frames, bool show_frame_info,
                                 uint32_t num_frames_with_source,
                                 bool show_unique, bool show_hidden,
                                 const char *selected_frame_marker) {
  size_t num_frames_displayed = 0;

  if (num_frames == 0)
    return 0;

  StackFrameSP frame_sp;
  uint32_t frame_idx = 0;
  uint32_t last_frame;

  // Don't let the last frame wrap around...
  if (num_frames == UINT32_MAX)
    last_frame = UINT32_MAX;
  else
    last_frame = first_frame + num_frames;

  StackFrameSP selected_frame_sp =
      m_thread.GetSelectedFrame(DoNoSelectMostRelevantFrame);
  const char *unselected_marker = nullptr;
  std::string buffer;
  if (selected_frame_marker) {
    size_t len = strlen(selected_frame_marker);
    buffer.insert(buffer.begin(), len, ' ');
    unselected_marker = buffer.c_str();
  }
  const char *marker = nullptr;
  for (frame_idx = first_frame; frame_idx < last_frame; ++frame_idx) {
    frame_sp = GetFrameAtIndex(frame_idx);
    if (!frame_sp)
      break;

    if (selected_frame_marker != nullptr) {
      if (frame_sp == selected_frame_sp)
        marker = selected_frame_marker;
      else
        marker = unselected_marker;
    }

    // Hide uninteresting frames unless it's the selected frame.
    if (!show_hidden && frame_sp != selected_frame_sp && frame_sp->IsHidden())
      continue;

    // Check for interruption here.  If we're fetching arguments, this loop
    // can go slowly:
    Debugger &dbg = m_thread.GetProcess()->GetTarget().GetDebugger();
    if (INTERRUPT_REQUESTED(
            dbg, "Interrupted dumping stack for thread {0:x} with {1} shown.",
            m_thread.GetID(), num_frames_displayed))
      break;


    if (!frame_sp->GetStatus(strm, show_frame_info,
                             num_frames_with_source > (first_frame - frame_idx),
                             show_unique, marker))
      break;
    ++num_frames_displayed;
  }

  strm.IndentLess();
  return num_frames_displayed;
}
