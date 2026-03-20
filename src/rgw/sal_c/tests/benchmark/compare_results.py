#!/usr/bin/env python3
"""
SAL C Benchmark Results Comparator

Compares C and C++ implementation benchmark results
"""

import json
import sys
from typing import Dict, List, Any


class BenchmarkComparator:
    """Compare benchmark results between C and C++ implementations"""

    def __init__(self, c_results_file: str, cpp_results_file: str):
        self.c_results_file = c_results_file
        self.cpp_results_file = cpp_results_file
        self.c_results: Dict[str, Any] = {}
        self.cpp_results: Dict[str, Any] = {}
        self.comparison: Dict[str, Dict] = {}

    def load_results(self) -> bool:
        """Load benchmark results from JSON files"""
        try:
            with open(self.c_results_file, 'r') as f:
                self.c_results = json.load(f)
        except FileNotFoundError:
            print(f"Error: C results file not found: {self.c_results_file}")
            return False
        except json.JSONDecodeError:
            print(f"Error: Invalid JSON in C results file: {self.c_results_file}")
            return False

        try:
            with open(self.cpp_results_file, 'r') as f:
                self.cpp_results = json.load(f)
        except FileNotFoundError:
            print(f"Warning: C++ results file not found: {self.cpp_results_file}")
            print("Will compare against baseline thresholds instead")
        except json.JSONDecodeError:
            print(f"Error: Invalid JSON in C++ results file: {self.cpp_results_file}")
            return False

        return True

    def compare_throughput(self, test_name: str) -> Dict[str, Any]:
        """Compare throughput between C and C++"""
        c_throughput = self.c_results['results'].get(test_name, {}).get('throughput', 0)
        cpp_throughput = self.cpp_results['results'].get(test_name, {}).get('throughput', 0)

        if cpp_throughput > 0:
            diff_pct = ((c_throughput - cpp_throughput) / cpp_throughput) * 100
        else:
            diff_pct = 0

        return {
            'c_throughput': c_throughput,
            'cpp_throughput': cpp_throughput,
            'difference_pct': diff_pct,
            'status': 'pass' if abs(diff_pct) < 10 else 'warning' if abs(diff_pct) < 20 else 'fail'
        }

    def compare_latency(self, test_name: str) -> Dict[str, Any]:
        """Compare latency between C and C++"""
        c_latencies = {
            'p50': self.c_results['results'].get(test_name, {}).get('p50', 0),
            'p95': self.c_results['results'].get(test_name, {}).get('p95', 0),
            'p99': self.c_results['results'].get(test_name, {}).get('p99', 0),
        }

        cpp_latencies = {
            'p50': self.cpp_results['results'].get(test_name, {}).get('p50', 0),
            'p95': self.cpp_results['results'].get(test_name, {}).get('p95', 0),
            'p99': self.cpp_results['results'].get(test_name, {}).get('p99', 0),
        }

        return {
            'c_latencies': c_latencies,
            'cpp_latencies': cpp_latencies,
            'status': 'pass'
        }

    def compare_all(self):
        """Compare all test results"""
        test_names = set(self.c_results['results'].keys())
        if self.cpp_results:
            test_names.update(self.cpp_results['results'].keys())

        for test_name in sorted(test_names):
            self.comparison[test_name] = {
                'throughput': self.compare_throughput(test_name),
                'latency': self.compare_latency(test_name)
            }

    def print_report(self):
        """Print comparison report"""
        print("\n" + "=" * 80)
        print("SAL C vs C++ BENCHMARK COMPARISON")
        print("=" * 80)

        for test_name, results in self.comparison.items():
            print(f"\n{test_name.upper().replace('_', ' ')}")
            print("-" * 40)

            tp = results['throughput']
            lt = results['latency']

            print(f"  Throughput:")
            print(f"    C++:       {tp['cpp_throughput']:.2f} ops/sec")
            print(f"    C:         {tp['c_throughput']:.2f} ops/sec")
            print(f"    Difference: {tp['difference_pct']:+.2f}%")
            print(f"    Status:     {tp['status'].upper()}")

            print(f"  Latency (C implementation):")
            print(f"    P50:       {lt['c_latencies']['p50']:.4f}ms")
            print(f"    P95:       {lt['c_latencies']['p95']:.4f}ms")
            print(f"    P99:       {lt['c_latencies']['p99']:.4f}ms")

        print("\n" + "=" * 80)
        print("STATUS LEGEND:")
        print("  PASS:    Difference < 10%")
        print("  WARNING: Difference 10-20%")
        print("  FAIL:    Difference > 20%")
        print("=" * 80)

    def save_report(self, filename: str):
        """Save comparison report to JSON"""
        with open(filename, 'w') as f:
            json.dump({
                'c_results': self.c_results,
                'cpp_results': self.cpp_results,
                'comparison': self.comparison
            }, f, indent=2)
        print(f"\nComparison report saved to: {filename}")

    def generate_markdown_report(self, filename: str):
        """Generate markdown report"""
        lines = [
            "# SAL C vs C++ Benchmark Comparison Report",
            "",
            "## Summary",
            "",
            "| Test | C++ Throughput | C Throughput | Difference | Status |",
            "|------|-----------------|--------------|------------|--------|"
        ]

        for test_name, results in self.comparison.items():
            tp = results['throughput']
            lines.append(f"| {test_name} | {tp['cpp_throughput']:.2f} ops/s | "
                        f"{tp['c_throughput']:.2f} ops/s | {tp['difference_pct']:+.2f}% | "
                        f"{tp['status'].upper()} |")

        lines.extend([
            "",
            "## Detailed Latency Results",
            "",
            "### C Implementation Latency",
            "",
            "| Test | P50 | P95 | P99 |",
            "|------|-----|-----|-----|"
        ])

        for test_name, results in self.comparison.items():
            lt = results['latency']
            lines.append(f"| {test_name} | {lt['c_latencies']['p50']:.4f}ms | "
                        f"{lt['c_latencies']['p95']:.4f}ms | {lt['c_latencies']['p99']:.4f}ms |")

        with open(filename, 'w') as f:
            f.write("\n".join(lines))

        print(f"Markdown report saved to: {filename}")


def main():
    """Main entry point"""
    import argparse

    parser = argparse.ArgumentParser(description='Compare SAL C and C++ benchmark results')
    parser.add_argument('--c-results', required=True, help='C implementation results JSON file')
    parser.add_argument('--cpp-results', default='cpp_benchmark_report.json',
                       help='C++ implementation results JSON file')
    parser.add_argument('--output', default='comparison_report.json',
                       help='Output comparison report JSON file')
    parser.add_argument('--markdown', default='comparison_report.md',
                       help='Output markdown report file')

    args = parser.parse_args()

    comparator = BenchmarkComparator(args.c_results, args.cpp_results)
    if not comparator.load_results():
        sys.exit(1)

    comparator.compare_all()
    comparator.print_report()
    comparator.save_report(args.output)
    comparator.generate_markdown_report(args.markdown)


if __name__ == '__main__':
    main()
