#!/bin/bash
#
# SAL C Benchmark Test Runner
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../../../../build"
SRC_DIR="${SCRIPT_DIR}/../../../../src/rgw/sal_c"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Functions
print_header() {
    echo ""
    echo "========================================"
    echo "$1"
    echo "========================================"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Main
main() {
    print_header "SAL C Implementation - Test Runner"
    
    # Check if build directory exists
    if [ ! -d "$BUILD_DIR" ]; then
        print_warning "Build directory not found: $BUILD_DIR"
        print_warning "Run cmake and make first to build the project"
    fi
    
    # Run Python benchmarks
    print_header "Running Python Benchmarks"
    python3 "${SRC_DIR}/tests/benchmark/sal_c_benchmark.py" \
        --mode all \
        --iterations 1000 \
        --object-size 1048576 \
        --output "${SCRIPT_DIR}/benchmark_report.json"
    
    if [ $? -eq 0 ]; then
        print_success "Benchmarks completed successfully"
    else
        print_error "Benchmarks failed"
    fi
    
    # Run functional tests
    print_header "Running Functional Tests"
    python3 -m pytest "${SRC_DIR}/tests/benchmark/test_functional.py" -v
    
    if [ $? -eq 0 ]; then
        print_success "Functional tests completed successfully"
    else
        print_error "Functional tests failed"
    fi
    
    print_header "Test Summary"
    echo "Report generated: ${SCRIPT_DIR}/benchmark_report.json"
    echo "Functional tests: ${SRC_DIR}/tests/benchmark/test_functional.py"
}

main "$@"
