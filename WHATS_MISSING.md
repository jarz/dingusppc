# What's Missing / Should Be Implemented Next

## Based on Honest Assessment of Current State

This document enumerates what's missing or should be implemented next, WITHOUT actually implementing it.

---

## Category 1: Validation Gaps (Close the Testing Loop)

### 1.1 Real Execution Tests for IABR Breakpoints
**Current State:** Mechanism integrated, code exists, NOT execution-tested  
**What's Missing:**
- Tests that actually execute PowerPC code
- Verify breakpoint triggers when PC matches IABR
- Validate EXC_TRACE exception is properly delivered
- Test breakpoint with debugger stop mechanism
- Test multiple breakpoints in sequence
- Test breakpoint enable/disable without reset

**Estimated Effort:** 8-12 hours  
**Priority:** HIGH (proves "breakpoints work" claim)

### 1.2 Real Execution Tests for PMC1 Instruction Counting
**Current State:** Code exists but disabled by default, NOT execution-tested  
**What's Missing:**
- Tests that execute code with counters enabled
- Verify PMC1 increments for each instruction
- Test MMCR0_FC freeze control during execution
- Test MMCR0_FCS/FCP privilege-based freezing
- Test overflow detection and logging
- Validate counter accuracy (count matches instructions executed)

**Estimated Effort:** 8-12 hours  
**Priority:** HIGH (validates profiling foundation)

### 1.3 Mac OS X Boot Testing
**Current State:** Registers implemented, OS never tested  
**What's Missing:**
- Actual Mac OS X boot sequence testing
- Document boot progress (where it succeeds, where it fails)
- Identify missing SPR functionality for OS compatibility
- Test common OS operations (context switching, exceptions)
- Validate SPR usage matches OS expectations

**Estimated Effort:** 16-24 hours  
**Priority:** MEDIUM (proves "Mac OS X compatibility")

---

## Category 2: Feature Completion (Make Infrastructure Functional)

### 2.1 DABR Data Breakpoint Integration
**Current State:** Configuration logging only, not integrated with memory access  
**What's Missing:**
- Hook DABR checks into memory read operations
- Hook DABR checks into memory write operations  
- Respect DABR_DR (read enable) and DABR_DW (write enable)
- Respect DABR_BT (translation enable)
- Trigger appropriate exception on match
- Test with real memory access patterns

**Estimated Effort:** 6-10 hours  
**Priority:** MEDIUM (completes hardware breakpoint support)

### 2.2 Enable PMC1 by Default (or Make It Easy)
**Current State:** Disabled by default, requires -DENABLE_PERFORMANCE_COUNTERS  
**What's Missing:**
- Consider making it a runtime option instead of compile-time
- Add CMake option to enable easily
- Document performance trade-off clearly
- Provide pre-built binaries with/without counters
- Add runtime enable/disable mechanism

**Estimated Effort:** 4-6 hours  
**Priority:** LOW (usability improvement)

### 2.3 Performance Monitor Exception Infrastructure
**Current State:** Overflow detected and logged, exception not triggered  
**What's Missing:**
- Actually trigger performance monitor interrupt
- Set appropriate SRR0/SRR1 values
- Handle exception in exception handler
- Test overflow exception delivery
- Document exception behavior

**Estimated Effort:** 6-10 hours  
**Priority:** MEDIUM (completes PMC functionality)

### 2.4 Multi-Event Performance Counting
**Current State:** PMC1 counts instructions only  
**What's Missing:**
- Event selection from MMCR0/MMCR1
- PMC2 for branch counting
- PMC3/PMC4 for cache events
- Multiple counter configurations
- Test different event combinations

**Estimated Effort:** 16-24 hours  
**Priority:** LOW (advanced profiling)

---

## Category 3: Debugger Integration (Make It Useful)

### 3.1 Debugger Commands for Breakpoints
**Current State:** Registers can be set, no debugger interface  
**What's Missing:**
- `break <address>` command to set IABR
- `watch <address>` command to set DABR
- `info breakpoints` to display status
- `delete breakpoint` to clear
- Integrate with existing debugger infrastructure

**Estimated Effort:** 8-12 hours  
**Priority:** HIGH (makes breakpoints usable)

### 3.2 Debugger Stop/Continue on Breakpoint
**Current State:** EXC_TRACE triggered, no debugger integration  
**What's Missing:**
- Catch trace exception in debugger
- Stop execution and enter debugger
- Display breakpoint hit information
- Allow user to continue, step, or examine state
- Test interactive debugging workflow

**Estimated Effort:** 12-16 hours  
**Priority:** HIGH (essential for debugging)

### 3.3 Conditional Breakpoints
**Current State:** IABR/DABR are simple address matches  
**What's Missing:**
- Break only on Nth hit
- Break when condition is true (register value, etc.)
- Temporary breakpoints (one-shot)
- Breakpoint hit counters
- Scripted breakpoint actions

**Estimated Effort:** 16-24 hours  
**Priority:** LOW (advanced debugging)

---

## Category 4: Advanced SPR Features (Optional Enhancements)

### 4.1 Actual L2 Cache Simulation
**Current State:** L2CR configuration logged, no cache behavior  
**What's Missing:**
- Actual L2 cache implementation
- Cache hit/miss simulation
- Cache line allocation/eviction
- Cache invalidation effects
- Performance impact simulation

**Estimated Effort:** 60-120 hours  
**Priority:** VERY LOW (research-grade accuracy)

### 4.2 Thermal Management Simulation
**Current State:** THRM1-3 configuration logged, no thermal behavior  
**What's Missing:**
- CPU temperature simulation
- Thermal throttling simulation
- Thermal interrupt generation
- Thermal monitoring state machine

**Estimated Effort:** 16-32 hours  
**Priority:** VERY LOW (rarely needed)

### 4.3 Additional PowerPC 60x SPRs
**Current State:** G3/G4 registers complete, some 603/604 missing  
**What's Missing:**
- PowerPC 603-specific SPRs
- PowerPC 604-specific SPRs  
- Additional 750/7400/7450 SPRs
- AltiVec-related SPRs (if not present)

**Estimated Effort:** 8-16 hours  
**Priority:** LOW (completeness)

### 4.4 SPR Access Tracing
**Current State:** Some logging exists, not comprehensive  
**What's Missing:**
- Trace all SPR reads/writes
- Log caller information (PC)
- Statistical analysis of SPR usage
- Performance profiling tool integration

**Estimated Effort:** 6-10 hours  
**Priority:** LOW (debugging aid)

---

## Category 5: Documentation & Testing (Quality Improvements)

### 5.1 Real-World Usage Examples
**Current State:** Tests exist but don't show real usage  
**What's Missing:**
- Example: Setting IABR breakpoint via debugger
- Example: Using PMC1 to profile code
- Example: Thermal monitoring in Mac OS
- Example: BAT register configuration for OS
- Tutorial documentation

**Estimated Effort:** 8-12 hours  
**Priority:** MEDIUM (user education)

### 5.2 Integration Test Suite
**Current State:** Unit tests only, no integration tests  
**What's Missing:**
- End-to-end debugging workflow test
- End-to-end profiling workflow test
- OS boot integration test
- Multi-feature integration tests

**Estimated Effort:** 12-20 hours  
**Priority:** MEDIUM (quality assurance)

### 5.3 Performance Regression Testing
**Current State:** bench1 exists, not automated for SPR changes  
**What's Missing:**
- Automated performance benchmarks
- CI integration for performance tests
- Performance regression detection
- Baseline comparison reports

**Estimated Effort:** 6-10 hours  
**Priority:** LOW (CI/CD improvement)

---

## Category 6: Code Quality (Technical Debt)

### 6.1 Remove TODO Comments
**Current State:** Several TODOs in implementation  
**What's Missing:**
- Review all TODO comments
- Implement missing functionality or document why not needed
- Remove completed TODOs
- Clean up code comments

**Estimated Effort:** 2-4 hours  
**Priority:** LOW (cleanup)

### 6.2 Consolidate Test Suites
**Current State:** 5 separate test executables  
**What's Missing:**
- Combine into unified test suite
- Share test infrastructure
- Reduce code duplication
- Streamline test execution

**Estimated Effort:** 4-6 hours  
**Priority:** LOW (maintainability)

### 6.3 Make Performance Counters Runtime-Selectable
**Current State:** Compile-time #ifdef  
**What's Missing:**
- Runtime enable/disable flag
- Configuration option in settings
- Toggle during execution
- No recompilation needed

**Estimated Effort:** 6-10 hours  
**Priority:** MEDIUM (flexibility)

---

## Prioritized Recommendations

### Immediate (Should Do Next):

1. **Real Execution Tests for IABR** (8-12h) - Validates "breakpoints work" claim
2. **Debugger Commands** (8-12h) - Makes breakpoints actually usable
3. **Debugger Stop/Continue** (12-16h) - Essential for debugging workflow

**Total: 28-40 hours** for usable hardware debugging

### Short-Term (Nice to Have):

4. **Real Execution Tests for PMC1** (8-12h) - Validates profiling foundation
5. **DABR Memory Access Integration** (6-10h) - Completes data breakpoints
6. **Real-World Usage Examples** (8-12h) - User education

**Total: 22-34 hours** for validated, documented features

### Long-Term (Optional):

7. **Mac OS X Boot Testing** (16-24h) - Proves OS compatibility
8. **Performance Monitor Exceptions** (6-10h) - Complete PMC feature
9. **Multi-Event Counting** (16-24h) - Advanced profiling

**Total: 38-58 hours** for complete feature set

---

## Summary by Priority

### HIGH Priority (Validation & Usability):
- Real execution tests (IABR, PMC1)
- Debugger integration (commands, stop/continue)
- **Total: 44-64 hours**

### MEDIUM Priority (Completeness):
- DABR integration
- Mac OS X testing
- Performance exceptions
- Usage examples
- **Total: 44-68 hours**

### LOW Priority (Polish):
- Multi-event counting
- Advanced debugging features
- Cache simulation
- Code cleanup
- **Total: 90-180+ hours**

---

## What's NOT Missing (Already Solid):

✅ SPR register read/write operations  
✅ Invalid SPR validation  
✅ Privilege checking  
✅ Configuration logging  
✅ G3/G4 register coverage  
✅ Test infrastructure  
✅ Documentation structure  

---

## Conclusion

The current implementation is a **solid foundation**. What's missing is:

1. **Validation** - Tests that prove features work in execution
2. **Integration** - Connect infrastructure to debugger/profiler
3. **Completion** - Finish partially-implemented features (DABR, PMC exceptions)

The most valuable next steps are:
- Real execution tests (~16-24 hours)
- Debugger integration (~20-28 hours)  
- DABR completion (~6-10 hours)

**Total for "feature complete" validation: ~42-62 hours**

This would transform the implementation from "foundation" to "production-ready debugging and profiling system."
