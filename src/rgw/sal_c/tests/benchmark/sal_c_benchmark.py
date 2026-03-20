#!/usr/bin/env python3
"""
SAL C Implementation Benchmark Suite

This script provides performance benchmarks and functional tests for the SAL C
implementation, comparing against the original C++ implementation.
"""

import time
import sys
import os
import statistics
from typing import List, Dict, Any, Optional

# Add the benchmark directory to path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


class BenchmarkConfig:
    """Benchmark configuration"""
    def __init__(self):
        self.iterations = 1000
        self.object_size = 1024 * 1024  # 1MB
        self.bucket_count = 100
        self.user_count = 100
        self.warmup_iterations = 100
        self.timeout = 300  # seconds


class BenchmarkResult:
    """Benchmark result container"""
    def __init__(self, name: str):
        self.name = name
        self.iterations = 0
        self.total_time = 0.0
        self.min_time = float('inf')
        self.max_time = 0.0
        self.avg_time = 0.0
        self.stddev = 0.0
        self.throughput = 0.0
        self.latencies: List[float] = []

    def calculate(self):
        """Calculate statistics"""
        if self.iterations > 0:
            self.avg_time = self.total_time / self.iterations
            self.throughput = self.iterations / self.total_time if self.total_time > 0 else 0

        if len(self.latencies) > 1:
            self.stddev = statistics.stdev(self.latencies)

        if self.latencies:
            self.min_time = min(self.latencies)
            self.max_time = max(self.latencies)

    def percentile(self, p: float) -> float:
        """Calculate percentile"""
        if not self.latencies:
            return 0.0
        sorted_latencies = sorted(self.latencies)
        idx = int(len(sorted_latencies) * p / 100.0)
        idx = min(idx, len(sorted_latencies) - 1)
        return sorted_latencies[idx]

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary"""
        return {
            'name': self.name,
            'iterations': self.iterations,
            'total_time': self.total_time,
            'min_time': self.min_time,
            'max_time': self.max_time,
            'avg_time': self.avg_time,
            'stddev': self.stddev,
            'throughput': self.throughput,
            'p50': self.percentile(50),
            'p95': self.percentile(95),
            'p99': self.percentile(99),
        }


class SALCTester:
    """SAL C Implementation Tester"""

    def __init__(self, driver_type: str = 'rados', config: Optional[BenchmarkConfig] = None):
        self.driver_type = driver_type
        self.config = config or BenchmarkConfig()
        self.results: Dict[str, BenchmarkResult] = {}

    def setup(self) -> bool:
        """Setup test environment"""
        print(f"[SETUP] Initializing SAL {self.driver_type} driver...")
        # TODO: Initialize the C driver through CFFI or ctypes
        return True

    def teardown(self):
        """Cleanup test environment"""
        print("[TEARDOWN] Cleaning up test environment...")

    # =================================================================
    # User Operations Tests
    # =================================================================

    def test_user_create(self) -> BenchmarkResult:
        """Test user creation throughput and latency"""
        result = BenchmarkResult('user_create')

        print(f"[TEST] User creation benchmark ({self.config.iterations} iterations)...")

        for i in range(self.config.warmup_iterations):
            self._create_user(f"warmup_user_{i}")

        for i in range(self.config.iterations):
            user_id = f"test_user_{i}"

            start = time.perf_counter()
            self._create_user(user_id)
            end = time.perf_counter()

            latency = (end - start) * 1000  # ms
            result.latencies.append(latency)
            result.iterations += 1
            result.total_time += (end - start)

            if i % 100 == 0:
                print(f"  Progress: {i}/{self.config.iterations}")

            self._delete_user(user_id)

        result.calculate()
        self.results['user_create'] = result
        return result

    def test_user_get(self) -> BenchmarkResult:
        """Test user retrieval throughput and latency"""
        result = BenchmarkResult('user_get')

        print(f"[TEST] User get benchmark ({self.config.iterations} iterations)...")

        # Create test users first
        test_users = [f"test_user_{i}" for i in range(self.config.user_count)]
        for user_id in test_users:
            self._create_user(user_id)

        for i in range(self.config.iterations):
            user_id = test_users[i % len(test_users)]

            start = time.perf_counter()
            self._get_user(user_id)
            end = time.perf_counter()

            latency = (end - start) * 1000  # ms
            result.latencies.append(latency)
            result.iterations += 1
            result.total_time += (end - start)

        # Cleanup
        for user_id in test_users:
            self._delete_user(user_id)

        result.calculate()
        self.results['user_get'] = result
        return result

    def test_user_update(self) -> BenchmarkResult:
        """Test user update throughput and latency"""
        result = BenchmarkResult('user_update')

        print(f"[TEST] User update benchmark ({self.config.iterations} iterations)...")

        # Create test users first
        test_users = [f"test_user_{i}" for i in range(self.config.user_count)]
        for user_id in test_users:
            self._create_user(user_id)

        for i in range(self.config.iterations):
            user_id = test_users[i % len(test_users)]

            start = time.perf_counter()
            self._update_user(user_id, f"Updated Name {i}")
            end = time.perf_counter()

            latency = (end - start) * 1000  # ms
            result.latencies.append(latency)
            result.iterations += 1
            result.total_time += (end - start)

        # Cleanup
        for user_id in test_users:
            self._delete_user(user_id)

        result.calculate()
        self.results['user_update'] = result
        return result

    def test_user_delete(self) -> BenchmarkResult:
        """Test user deletion throughput and latency"""
        result = BenchmarkResult('user_delete')

        print(f"[TEST] User delete benchmark ({self.config.iterations} iterations)...")

        for i in range(self.config.iterations):
            user_id = f"test_user_{i}"
            self._create_user(user_id)

            start = time.perf_counter()
            self._delete_user(user_id)
            end = time.perf_counter()

            latency = (end - start) * 1000  # ms
            result.latencies.append(latency)
            result.iterations += 1
            result.total_time += (end - start)

        result.calculate()
        self.results['user_delete'] = result
        return result

    # =================================================================
    # Bucket Operations Tests
    # =================================================================

    def test_bucket_create(self) -> BenchmarkResult:
        """Test bucket creation throughput and latency"""
        result = BenchmarkResult('bucket_create')

        print(f"[TEST] Bucket creation benchmark ({self.config.iterations} iterations)...")

        for i in range(self.config.iterations):
            bucket_name = f"testbucket{i}"

            start = time.perf_counter()
            self._create_bucket(bucket_name)
            end = time.perf_counter()

            latency = (end - start) * 1000  # ms
            result.latencies.append(latency)
            result.iterations += 1
            result.total_time += (end - start)

            self._delete_bucket(bucket_name)

        result.calculate()
        self.results['bucket_create'] = result
        return result

    def test_bucket_list(self) -> BenchmarkResult:
        """Test bucket listing throughput and latency"""
        result = BenchmarkResult('bucket_list')

        print(f"[TEST] Bucket list benchmark...")

        # Create test buckets first
        test_buckets = [f"testbucket{i}" for i in range(self.config.bucket_count)]
        for bucket_name in test_buckets:
            self._create_bucket(bucket_name)

        for i in range(self.config.iterations):
            start = time.perf_counter()
            buckets = self._list_buckets()
            end = time.perf_counter()

            latency = (end - start) * 1000  # ms
            result.latencies.append(latency)
            result.iterations += 1
            result.total_time += (end - start)

        # Cleanup
        for bucket_name in test_buckets:
            self._delete_bucket(bucket_name)

        result.calculate()
        self.results['bucket_list'] = result
        return result

    def test_bucket_delete(self) -> BenchmarkResult:
        """Test bucket deletion throughput and latency"""
        result = BenchmarkResult('bucket_delete')

        print(f"[TEST] Bucket delete benchmark ({self.config.iterations} iterations)...")

        for i in range(self.config.iterations):
            bucket_name = f"testbucket{i}"
            self._create_bucket(bucket_name)

            start = time.perf_counter()
            self._delete_bucket(bucket_name)
            end = time.perf_counter()

            latency = (end - start) * 1000  # ms
            result.latencies.append(latency)
            result.iterations += 1
            result.total_time += (end - start)

        result.calculate()
        self.results['bucket_delete'] = result
        return result

    # =================================================================
    # Object Operations Tests
    # =================================================================

    def test_object_put(self) -> BenchmarkResult:
        """Test object PUT throughput and latency"""
        result = BenchmarkResult('object_put')

        print(f"[TEST] Object PUT benchmark ({self.config.iterations} iterations, "
              f"{self.config.object_size} bytes)...")

        # Create test bucket
        self._create_bucket("testbucket")

        for i in range(self.config.iterations):
            object_name = f"testobject{i}"

            start = time.perf_counter()
            self._put_object("testbucket", object_name, self.config.object_size)
            end = time.perf_counter()

            latency = (end - start) * 1000  # ms
            result.latencies.append(latency)
            result.iterations += 1
            result.total_time += (end - start)

            self._delete_object("testbucket", object_name)

        # Cleanup
        self._delete_bucket("testbucket")

        result.calculate()
        self.results['object_put'] = result
        return result

    def test_object_get(self) -> BenchmarkResult:
        """Test object GET throughput and latency"""
        result = BenchmarkResult('object_get')

        print(f"[TEST] Object GET benchmark ({self.config.iterations} iterations)...")

        # Create test bucket and objects
        self._create_bucket("testbucket")
        for i in range(min(100, self.config.iterations)):
            self._put_object("testbucket", f"testobject{i}", self.config.object_size)

        for i in range(self.config.iterations):
            object_name = f"testobject{i % 100}"

            start = time.perf_counter()
            data = self._get_object("testbucket", object_name)
            end = time.perf_counter()

            latency = (end - start) * 1000  # ms
            result.latencies.append(latency)
            result.iterations += 1
            result.total_time += (end - start)

        # Cleanup
        for i in range(100):
            self._delete_object("testbucket", f"testobject{i}")
        self._delete_bucket("testbucket")

        result.calculate()
        self.results['object_get'] = result
        return result

    def test_object_list(self) -> BenchmarkResult:
        """Test object listing throughput and latency"""
        result = BenchmarkResult('object_list')

        print(f"[TEST] Object list benchmark...")

        # Create test bucket and objects
        self._create_bucket("testbucket")
        object_count = 1000
        for i in range(object_count):
            self._put_object("testbucket", f"testobject{i}", 1024)

        for i in range(self.config.iterations):
            start = time.perf_counter()
            objects = self._list_objects("testbucket")
            end = time.perf_counter()

            latency = (end - start) * 1000  # ms
            result.latencies.append(latency)
            result.iterations += 1
            result.total_time += (end - start)

        # Cleanup
        for i in range(object_count):
            self._delete_object("testbucket", f"testobject{i}")
        self._delete_bucket("testbucket")

        result.calculate()
        self.results['object_list'] = result
        return result

    def test_object_delete(self) -> BenchmarkResult:
        """Test object deletion throughput and latency"""
        result = BenchmarkResult('object_delete')

        print(f"[TEST] Object delete benchmark ({self.config.iterations} iterations)...")

        # Create test bucket
        self._create_bucket("testbucket")

        for i in range(self.config.iterations):
            object_name = f"testobject{i}"
            self._put_object("testbucket", object_name, self.config.object_size)

            start = time.perf_counter()
            self._delete_object("testbucket", object_name)
            end = time.perf_counter()

            latency = (end - start) * 1000  # ms
            result.latencies.append(latency)
            result.iterations += 1
            result.total_time += (end - start)

        # Cleanup
        self._delete_bucket("testbucket")

        result.calculate()
        self.results['object_delete'] = result
        return result

    # =================================================================
    # Internal Helper Methods (Stubs - to be implemented with CFFI)
    # =================================================================

    def _create_user(self, user_id: str) -> bool:
        """Create a test user"""
        # TODO: Call C SAL API through CFFI
        return True

    def _get_user(self, user_id: str) -> Optional[Dict]:
        """Get a test user"""
        # TODO: Call C SAL API through CFFI
        return {'user_id': user_id}

    def _update_user(self, user_id: str, display_name: str) -> bool:
        """Update a test user"""
        # TODO: Call C SAL API through CFFI
        return True

    def _delete_user(self, user_id: str) -> bool:
        """Delete a test user"""
        # TODO: Call C SAL API through CFFI
        return True

    def _create_bucket(self, bucket_name: str) -> bool:
        """Create a test bucket"""
        # TODO: Call C SAL API through CFFI
        return True

    def _get_bucket(self, bucket_name: str) -> Optional[Dict]:
        """Get a test bucket"""
        # TODO: Call C SAL API through CFFI
        return {'bucket_name': bucket_name}

    def _list_buckets(self) -> List[Dict]:
        """List test buckets"""
        # TODO: Call C SAL API through CFFI
        return []

    def _delete_bucket(self, bucket_name: str) -> bool:
        """Delete a test bucket"""
        # TODO: Call C SAL API through CFFI
        return True

    def _put_object(self, bucket_name: str, object_name: str, size: int) -> bool:
        """Put a test object"""
        # TODO: Call C SAL API through CFFI
        return True

    def _get_object(self, bucket_name: str, object_name: str) -> bytes:
        """Get a test object"""
        # TODO: Call C SAL API through CFFI
        return b''

    def _list_objects(self, bucket_name: str) -> List[str]:
        """List test objects"""
        # TODO: Call C SAL API through CFFI
        return []

    def _delete_object(self, bucket_name: str, object_name: str) -> bool:
        """Delete a test object"""
        # TODO: Call C SAL API through CFFI
        return True


class BenchmarkRunner:
    """Run benchmarks and generate reports"""

    def __init__(self, config: BenchmarkConfig):
        self.config = config
        self.sal_c_results: Dict[str, BenchmarkResult] = {}
        self.cpp_results: Dict[str, BenchmarkResult] = {}

    def run_all(self, mode: str = 'all') -> Dict[str, BenchmarkResult]:
        """Run all benchmarks"""
        tester = SALCTester(driver_type='c_sal', config=self.config)

        if not tester.setup():
            print("[ERROR] Failed to setup test environment")
            return {}

        results = {}

        if mode in ('all', 'user'):
            results['user_create'] = tester.test_user_create()
            results['user_get'] = tester.test_user_get()
            results['user_update'] = tester.test_user_update()
            results['user_delete'] = tester.test_user_delete()

        if mode in ('all', 'bucket'):
            results['bucket_create'] = tester.test_bucket_create()
            results['bucket_list'] = tester.test_bucket_list()
            results['bucket_delete'] = tester.test_bucket_delete()

        if mode in ('all', 'object'):
            results['object_put'] = tester.test_object_put()
            results['object_get'] = tester.test_object_get()
            results['object_list'] = tester.test_object_list()
            results['object_delete'] = tester.test_object_delete()

        tester.teardown()

        self.sal_c_results = results
        return results

    def print_report(self):
        """Print benchmark report"""
        print("\n" + "=" * 80)
        print("SAL C BENCHMARK RESULTS")
        print("=" * 80)

        for name, result in self.sal_c_results.items():
            print(f"\n{name.upper().replace('_', ' ')}")
            print("-" * 40)
            print(f"  Iterations:    {result.iterations}")
            print(f"  Total Time:    {result.total_time:.4f}s")
            print(f"  Avg Time:      {result.avg_time:.4f}ms")
            print(f"  Min Time:      {result.min_time:.4f}ms")
            print(f"  Max Time:      {result.max_time:.4f}ms")
            print(f"  Std Dev:       {result.stddev:.4f}ms")
            print(f"  Throughput:    {result.throughput:.2f} ops/sec")
            print(f"  P50 Latency:   {result.percentile(50):.4f}ms")
            print(f"  P95 Latency:   {result.percentile(95):.4f}ms")
            print(f"  P99 Latency:   {result.percentile(99):.4f}ms")

        print("\n" + "=" * 80)

    def save_report(self, filename: str):
        """Save benchmark report to file"""
        import json

        data = {
            'config': {
                'iterations': self.config.iterations,
                'object_size': self.config.object_size,
                'bucket_count': self.config.bucket_count,
                'user_count': self.config.user_count,
                'warmup_iterations': self.config.warmup_iterations,
            },
            'results': {name: result.to_dict() for name, result in self.sal_c_results.items()}
        }

        with open(filename, 'w') as f:
            json.dump(data, f, indent=2)

        print(f"\nReport saved to: {filename}")


def main():
    """Main entry point"""
    import argparse

    parser = argparse.ArgumentParser(description='SAL C Benchmark Suite')
    parser.add_argument('--mode', choices=['all', 'user', 'bucket', 'object'],
                       default='all', help='Test mode')
    parser.add_argument('--iterations', type=int, default=1000,
                       help='Number of iterations')
    parser.add_argument('--object-size', type=int, default=1024*1024,
                       help='Object size in bytes')
    parser.add_argument('--output', type=str, default='benchmark_report.json',
                       help='Output file for results')

    args = parser.parse_args()

    config = BenchmarkConfig()
    config.iterations = args.iterations
    config.object_size = args.object_size

    runner = BenchmarkRunner(config)
    runner.run_all(mode=args.mode)
    runner.print_report()
    runner.save_report(args.output)


if __name__ == '__main__':
    main()
