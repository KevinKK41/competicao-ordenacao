#!/usr/bin/env python3
"""Compila e compara Merge Sort sem alterar o baseline; somente stdlib."""
import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import shlex
import statistics
import subprocess
import tempfile


def executar(cmd, **kwargs):
    return subprocess.run(cmd, check=True, text=True, **kwargs)


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, default=repo / "ordem.c",
                        help="baseline com mergeSort32/64 que escrevem em v")
    parser.add_argument("--experimental", type=Path,
                        default=repo / "ordenacao_mergesort_experimental.c")
    parser.add_argument("--test", action="store_true", help="corretude exata contra qsort")
    parser.add_argument("--sanitize", action="store_true", help="ASan + UBSan (nao medir)")
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--flags", default="-O3 -march=native -flto")
    parser.add_argument("--block", type=int, default=64)
    parser.add_argument("--experimental-block", type=int, default=8,
                        help="bloco experimental int32 (padrao: 8, para AVX2)")
    parser.add_argument("--experimental-block64", type=int, default=4)
    parser.add_argument("--tile", type=int, default=16384)
    parser.add_argument("--double-merge", type=int, choices=(0, 1), default=1)
    parser.add_argument("--avx2", type=int, choices=(0, 1), default=1)
    parser.add_argument("--simd-branchless", type=int, choices=(0, 1), default=1)
    parser.add_argument("--simd-two-ended", type=int, choices=(0, 1), default=1)
    parser.add_argument("--n", type=int, default=10_000_000)
    parser.add_argument("--repeats", type=int, default=10)
    parser.add_argument("--seeds", type=int, nargs="+", default=[41, 2026, 5700])
    parser.add_argument("--bits", type=int, choices=(32, 64), default=64)
    parser.add_argument("--pattern", type=int, choices=range(9), default=0)
    parser.add_argument("--cpu", type=int, help="CPU logica; padrao: primeira permitida")
    parser.add_argument("--output", type=Path, help="CSV e metadados .json; fora do checkout")
    args = parser.parse_args()
    experimental_block = args.experimental_block
    if args.sanitize and not args.test:
        parser.error("--sanitize requer --test; resultados instrumentados nao sao benchmarks")
    if args.n < 0 or args.repeats < 1 or args.block < 2 or experimental_block < 2 or args.tile < experimental_block:
        parser.error("n/repeats/block/tile invalidos")
    razao, resto = divmod(args.tile, experimental_block)
    if resto or razao & (razao - 1):
        parser.error("tile deve ser block vezes potencia de dois")
    if args.experimental_block64 < 2 or args.tile < args.experimental_block64:
        parser.error("bloco de 64 bits invalido")
    razao64, resto64 = divmod(args.tile, args.experimental_block64)
    if resto64 or razao64 & (razao64 - 1):
        parser.error("tile deve ser block64 vezes potencia de dois")
    baseline, experimental = args.baseline.resolve(), args.experimental.resolve()
    afinidade = None
    if hasattr(os, "sched_getaffinity"):
        permitidas = sorted(os.sched_getaffinity(0))
        cpu = args.cpu if args.cpu is not None else permitidas[0]
        if cpu not in permitidas:
            parser.error(f"CPU {cpu} indisponivel; escolha entre {permitidas}")
        os.sched_setaffinity(0, {cpu})
        afinidade = cpu
    flags = ["-std=c11", "-Wall", "-Wextra", "-Wpedantic"]
    flags += (["-O1", "-g", "-march=native", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
               "-fno-pie", "-no-pie"] if args.sanitize else shlex.split(args.flags))
    flags += [f"-DBLOCO_INICIAL={args.block}"]
    metadata = {
        "baseline": str(baseline), "experimental": str(experimental),
        "baseline_sha256": hashlib.sha256(baseline.read_bytes()).hexdigest(),
        "experimental_sha256": hashlib.sha256(experimental.read_bytes()).hexdigest(),
        "compiler": executar([args.cc, "--version"], capture_output=True).stdout.splitlines()[0],
        "flags": flags, "tile": args.tile, "double_merge": args.double_merge,
        "experimental_block": experimental_block,
        "experimental_block64": args.experimental_block64,
        "avx2": args.avx2,
        "simd_branchless": args.simd_branchless,
        "simd_two_ended": args.simd_two_ended,
        "platform": platform.platform(), "cpu_affinity": afinidade,
        "n": args.n, "bits": args.bits, "seeds": args.seeds,
        "repeats": args.repeats, "pattern": args.pattern,
        "warmup_pairs_per_seed": 1, "scratch_pretouched": True,
    }
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.exists():
        metadata["cpu_model"] = next((line.split(":", 1)[1].strip()
            for line in cpuinfo.read_text().splitlines() if line.startswith("model name")), "unknown")
    with tempfile.TemporaryDirectory(prefix="merge-bench-") as tmp:
        tmp = Path(tmp)
        objects = []
        for exp, fonte in enumerate((baseline, experimental)):
            obj = tmp / f"adapter{exp}.o"
            defines = [f'-DMERGE_SOURCE="{fonte}"']
            if exp:
                defines += ["-DEXPERIMENTAL_ADAPTER", f"-DTILE_ELEMENTOS={args.tile}",
                            f"-DMESCLA_DUPLA={args.double_merge}",
                            f"-DMESCLA_AVX2={args.avx2}",
                            f"-DBLOCO_INICIAL_64={args.experimental_block64}",
                            f"-DAVX2_SEM_DESVIO={args.simd_branchless}",
                            f"-DAVX2_DUAS_PONTAS={args.simd_two_ended}"]
            compile_flags = flags if not exp else [flag for flag in flags
                if not flag.startswith("-DBLOCO_INICIAL=")] + [f"-DBLOCO_INICIAL={experimental_block}"]
            executar([args.cc, *compile_flags, *defines, "-c", str(repo / "tests/mergesort_adapter.c"),
                      "-o", str(obj)])
            objects.append(str(obj))
        driver = tmp / "driver"
        executar([args.cc, *flags, str(repo / "tests/mergesort_driver.c"), *objects,
                  "-o", str(driver)])
        if args.test:
            executar([str(driver), "test"])
            return
        rows = []
        for seed in args.seeds:
            result = executar([str(driver), "bench", str(args.n), str(args.repeats),
                               str(seed), str(args.bits), str(args.pattern)], capture_output=True)
            batch = list(csv.DictReader(io.StringIO(result.stdout)))
            if len(batch) != 2 * args.repeats or any(row["correto"] != "1" for row in batch):
                raise RuntimeError("Benchmark incompleto ou incorreto")
            rows.extend(batch)
            print(f"seed {seed}: {len(batch)} medicoes verificadas", flush=True)
    for variant in ("baseline", "experimental"):
        selected = [row for row in rows if row["variante"] == variant]
        for clock in ("cpu_ms", "wall_ms"):
            values = [float(row[clock]) for row in selected]
            print(f"{variant} {clock}: media={statistics.mean(values):.3f} "
                  f"mediana={statistics.median(values):.3f} min={min(values):.3f} "
                  f"max={max(values):.3f} desvio={statistics.stdev(values) if len(values)>1 else 0:.3f}")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("w", newline="") as out:
            writer = csv.DictWriter(out, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        args.output.with_suffix(args.output.suffix + ".json").write_text(
            json.dumps(metadata, indent=2) + "\n")
        print(f"CSV: {args.output}")


if __name__ == "__main__":
    main()
