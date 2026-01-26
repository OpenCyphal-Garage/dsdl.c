#!/usr/bin/env python3
"""
Generate Nunavut C code for all test DSDL types.
This script invokes nnvg to generate C serialization code for cross-validation testing.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    # Isolate from external DSDL namespace pollution
    os.environ.pop('CYPHAL_PATH', None)
    os.environ.pop('DSDL_INCLUDE_PATH', None)
    parser = argparse.ArgumentParser(description='Generate Nunavut C code for test namespaces')
    parser.add_argument('--output-dir', type=Path, required=True,
                        help='Output directory for generated code')
    parser.add_argument('--test-root', type=Path, required=True,
                        help='Root directory containing test_dsdl_root_namespaces')
    parser.add_argument('--verbose', action='store_true',
                        help='Enable verbose output')
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)

    ns_dir_0 = args.test_root / 'test_dsdl_root_namespaces' / '0'
    ns_dir_1 = args.test_root / 'test_dsdl_root_namespaces' / '1'

    if not ns_dir_0.exists():
        print(f"Error: Test namespace directory not found: {ns_dir_0}", file=sys.stderr)
        return 1

    if not ns_dir_1.exists():
        print(f"Error: Test namespace directory not found: {ns_dir_1}", file=sys.stderr)
        return 1

    root_namespaces_0 = sorted([d for d in ns_dir_0.iterdir() if d.is_dir()])
    root_namespaces_1 = sorted([d for d in ns_dir_1.iterdir() if d.is_dir() and d.name != 'invalid'])
    
    all_namespaces = root_namespaces_0 + root_namespaces_1
    
    print(f"Found {len(root_namespaces_0)} root namespaces in {ns_dir_0}")
    print(f"Found {len(root_namespaces_1)} root namespaces in {ns_dir_1}")
    print(f"Total: {len(all_namespaces)} root namespaces")
    
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp_path = Path(tmpdir)
        
        for ns_path in all_namespaces:
            link_path = tmp_path / ns_path.name
            if not link_path.exists():
                os.symlink(ns_path.resolve(), link_path)
        
        print(f"\nCreated temporary flat namespace directory: {tmp_path}")
        
        success_count = 0
        failed_namespaces = []
        
        for ns_path in all_namespaces:
            ns_name = ns_path.name
            print(f"\nGenerating code for namespace: {ns_name}")
            
            ns_tmp_dir = Path(tempfile.mkdtemp())
            ns_link = ns_tmp_dir / ns_name
            os.symlink(ns_path.resolve(), ns_link)
            
            for other_ns in all_namespaces:
                if other_ns != ns_path:
                    other_link = ns_tmp_dir / other_ns.name
                    if not other_link.exists():
                        os.symlink(other_ns.resolve(), other_link)
            
            cmd = [
                'nnvg',
                '--target-language', 'c',
                '--enable-serialization-asserts',
                '--enable-override-variable-array-capacity',
                '-O', str(args.output_dir),
                str(ns_link),
                '--lookup-dir', str(ns_tmp_dir),
            ]
            
            if args.verbose:
                print(f"Running: {' '.join(cmd)}")
            
            try:
                result = subprocess.run(cmd, check=True, capture_output=True, text=True, timeout=60)
                if args.verbose and result.stdout:
                    print(result.stdout)
                if result.stderr:
                    print(result.stderr, file=sys.stderr)
                success_count += 1
                print(f"  ✓ Success")
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
                print(f"  ✗ Failed (skipping)")
                failed_namespaces.append(ns_name)
                if args.verbose:
                    if hasattr(e, 'stderr') and e.stderr:
                        print(e.stderr, file=sys.stderr)
            finally:
                shutil.rmtree(ns_tmp_dir, ignore_errors=True)

    print(f"\n{'='*60}")
    print(f"Summary:")
    print(f"  Successfully generated: {success_count}/{len(all_namespaces)} namespaces")
    if failed_namespaces:
        print(f"  Failed namespaces: {', '.join(failed_namespaces)}")
    print(f"  Output directory: {args.output_dir}")
    print(f"{'='*60}")
    
    return 0 if success_count > 0 else 1


if __name__ == '__main__':
    sys.exit(main())
