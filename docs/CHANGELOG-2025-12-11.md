# Changelog - December 11, 2025

## Summary
Comprehensive code analysis was conducted on the Kartend application, resulting in 18 prioritized recommendations. Critical and high-priority improvements were implemented.

## New Features

### CI/CD Pipeline
- **File**: `.github/workflows/build.yml`
- Automated builds on push/PR to main branch
- Matrix strategy for Release and Debug configurations
- Qt6 dependencies installed automatically
- Test execution with `BUILD_TESTS=ON`
- Artifact upload for release builds
- Weekly maintenance builds (Sundays at 2 AM UTC)

### MIT License
- **File**: `LICENSE`
- Standard MIT License added for open-source compatibility
- License section added to `readme.md`

## Security Fixes

### RetroArch Core Path Argument Injection (Critical)
- **File**: `src/modules/launch/launchmanager.cpp` (lines 246-280)
- **Issue**: Core paths could be crafted to inject command-line arguments
- **Fix**: Added validation to reject:
  - Paths starting with `-` (direct flag injection)
  - Paths containing ` -` (embedded flag injection)
- **Impact**: Prevents malicious users from executing arbitrary RetroArch commands

## New Tests

### CacheManager Tests
- **File**: `tests/test_cachemanager.cpp`
- 15 test cases covering:
  - Basic cache operations (insert, retrieve, miss)
  - Empty/null path handling
  - Size threshold enforcement (MIN_PIXMAP_SIZE)
  - Cache metrics (hit rate, memory hits, misses)
  - Cache management (clear, release resources)

### SessionManager Tests
- **File**: `tests/test_sessionmanager.cpp`
- 12 test cases covering:
  - Last selected state persistence
  - Suffix matching for hierarchical names
  - Global item count tracking
  - Collection counts (set/get/hierarchical)
  - Stale collection cleanup

### Test Infrastructure Updates
- **File**: `tests/CMakeLists.txt`
- Added CacheManager and SessionManager test targets
- Updated include directories for new modules

## Documentation

### Analysis Report
- **File**: `docs/analysis-report.md`
- Comprehensive 18-recommendation report covering:
  - Architecture assessment
  - Code quality findings
  - Performance considerations
  - Security posture
  - Scalability analysis
  - Maintainability review
  - User experience evaluation
- Each recommendation includes:
  - Priority level (Critical/High/Medium/Low)
  - Estimated effort
  - Implementation guidance
  - Rationale and impact

## Files Changed

| File | Change Type | Description |
|------|-------------|-------------|
| `.github/workflows/build.yml` | Added | CI/CD pipeline |
| `LICENSE` | Added | MIT License |
| `docs/analysis-report.md` | Added | Code analysis report |
| `docs/CHANGELOG-2025-12-11.md` | Added | This changelog |
| `readme.md` | Modified | Added License section |
| `src/modules/launch/launchmanager.cpp` | Modified | Security validation for core paths |
| `tests/CMakeLists.txt` | Modified | Added new test targets |
| `tests/test_cachemanager.cpp` | Added | CacheManager unit tests |
| `tests/test_sessionmanager.cpp` | Added | SessionManager unit tests |

## Remaining Recommendations (For Future Work)

### High Priority
- Implement parallel directory scanning in `QueryManager::scanMediaDirectory()`
- Consider extracting MenuController from MainWindow (marginal benefit)

### Medium Priority
- Add more test coverage for DatabaseManager, QueryManager
- Implement adaptive search debouncing based on collection size
- Add telemetry for cache hit rates

### Low Priority
- Replace custom error dialog with native Qt error handling
- Add keyboard shortcut customization
- Implement collection export/import functionality

## Commits
```
955177b Remove extraneous icons folder not in main
95eda0e Restore resources.qrc from main branch
3a831f1 Restore assets (icon.svg, kartend.desktop) from main branch
fab0757 Implement high-priority improvements from code analysis