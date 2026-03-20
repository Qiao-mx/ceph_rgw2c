#!/usr/bin/env python3
"""
SAL C Benchmark Report Generator

Generates HTML report from benchmark results
"""

import json
import sys
from datetime import datetime
from typing import Dict, Any


def generate_html_report(benchmark_results: Dict[str, Any], output_file: str):
    """Generate HTML report from benchmark results"""

    html = """<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>SAL C Benchmark Report</title>
    <style>
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            margin: 0;
            padding: 20px;
            background-color: #f5f5f5;
        }
        .container {
            max-width: 1200px;
            margin: 0 auto;
            background: white;
            padding: 30px;
            border-radius: 8px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        h1 {
            color: #333;
            border-bottom: 2px solid #4CAF50;
            padding-bottom: 10px;
        }
        h2 {
            color: #555;
            margin-top: 30px;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            margin: 20px 0;
        }
        th, td {
            padding: 12px;
            text-align: left;
            border-bottom: 1px solid #ddd;
        }
        th {
            background-color: #4CAF50;
            color: white;
        }
        tr:hover {
            background-color: #f5f5f5;
        }
        .metric {
            font-weight: bold;
            color: #2196F3;
        }
        .value {
            font-family: monospace;
        }
        .chart-container {
            margin: 20px 0;
        }
        .summary {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 20px;
            margin: 20px 0;
        }
        .summary-card {
            background: #f9f9f9;
            padding: 20px;
            border-radius: 8px;
            text-align: center;
        }
        .summary-card h3 {
            margin: 0;
            color: #666;
        }
        .summary-card .value {
            font-size: 24px;
            color: #4CAF50;
            font-weight: bold;
        }
        .pass {
            color: #4CAF50;
        }
        .warning {
            color: #FF9800;
        }
        .fail {
            color: #F44336;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>SAL C Benchmark Report</h1>
        <p>Generated: """ + datetime.now().strftime("%Y-%m-%d %H:%M:%S") + """</p>
        
        <h2>Summary</h2>
        <div class="summary">
            <div class="summary-card">
                <h3>Total Tests</h3>
                <div class="value">""" + str(len(benchmark_results.get('results', {}))) + """</div>
            </div>
        </div>
        
        <h2>Results</h2>
        <table>
            <thead>
                <tr>
                    <th>Test Name</th>
                    <th>Iterations</th>
                    <th>Throughput</th>
                    <th>Avg Time</th>
                    <th>P50</th>
                    <th>P95</th>
                    <th>P99</th>
                </tr>
            </thead>
            <tbody>
"""

        for test_name, result in benchmark_results.get('results', {}).items():
            html += f"""
                <tr>
                    <td>{test_name}</td>
                    <td class="value">{result.get('iterations', 0)}</td>
                    <td class="value metric">{result.get('throughput', 0):.2f} ops/s</td>
                    <td class="value">{result.get('avg_time', 0):.4f} ms</td>
                    <td class="value">{result.get('p50', 0):.4f} ms</td>
                    <td class="value">{result.get('p95', 0):.4f} ms</td>
                    <td class="value">{result.get('p99', 0):.4f} ms</td>
                </tr>
"""

        html += """
            </tbody>
        </table>
    </div>
</body>
</html>
"""

    with open(output_file, 'w') as f:
        f.write(html)

    print(f"HTML report generated: {output_file}")


def main():
    """Main entry point"""
    import argparse

    parser = argparse.ArgumentParser(description='Generate HTML report from benchmark results')
    parser.add_argument('--input', required=True, help='Input benchmark results JSON file')
    parser.add_argument('--output', default='benchmark_report.html', help='Output HTML file')

    args = parser.parse_args()

    try:
        with open(args.input, 'r') as f:
            benchmark_results = json.load(f)
    except FileNotFoundError:
        print(f"Error: File not found: {args.input}")
        sys.exit(1)
    except json.JSONDecodeError:
        print(f"Error: Invalid JSON file: {args.input}")
        sys.exit(1)

    generate_html_report(benchmark_results, args.output)


if __name__ == '__main__':
    main()
