"""
run_geant4_pp4.py - Parallel worker for Geant4 Simulation (2026_8_2_G4_pp4)
Runs on GitHub Actions Swarm (13 active accounts x 10 runners).
Uploads raw 1-to-1 CSV results to Flat Common Pool (02_Flux_Data) without account subfolders.
Uploads child outputs (.root files) to 03_Simulation_Outputs.
"""

import argparse
import os
import sys
import time
import json
import zipfile
import subprocess
from pathlib import Path
import pandas as pd

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

import gdrive_helper
import job_naming

def parse_args():
    parser = argparse.ArgumentParser(description="Geant4 Parallel Worker for 2026_8_2_G4_pp4")
    parser.add_argument("--runner-id", type=int, required=True, help="ID runner (1..10)")
    parser.add_argument("--num-runners", type=int, default=10, help="Tong so runner tren account nay")
    parser.add_argument("--primaries", type=int, default=1000000, help="So hat moi job")
    parser.add_argument("--gdrive-folder", type=str, default="1czPwTZ5on2pNreZKOLyvS7TdoBz_bcMD")
    parser.add_argument("--plan-file", type=str, default="pp4_plan_14acc.json")
    parser.add_argument("--github-user", type=str, default=None)
    parser.add_argument("--sim-bin", type=str, default=None)
    parser.add_argument("--timeout-hours", type=float, default=2.0)
    return parser.parse_args()

def ensure_compiled():
    candidates = ["./build/Sim", "./Sim", "/tmp/geant4_build/Sim"]
    for c in candidates:
        if os.path.exists(c) and os.access(c, os.X_OK):
            return c
    
    print(">>> [BUILD] Bien dich ma nguon Geant4 Sim...")
    build_dir = Path("build")
    build_dir.mkdir(parents=True, exist_ok=True)
    res_cmake = subprocess.run(["cmake", "..", "-DCMAKE_BUILD_TYPE=Release"], cwd=str(build_dir))
    if res_cmake.returncode != 0:
        raise RuntimeError("CMake configure that bai!")
    res_make = subprocess.run(["make", f"-j{os.cpu_count() or 2}", "Sim"], cwd=str(build_dir))
    if res_make.returncode != 0:
        raise RuntimeError("Make Sim that bai!")
    
    sim_bin = "./build/Sim"
    if not os.path.exists(sim_bin):
        raise FileNotFoundError(f"Khong tim thay binary {sim_bin} sau khi build!")
    return sim_bin

def parse_process_results_last_row(csv_path: Path):
    if not csv_path.exists():
        return {}
    try:
        df = pd.read_csv(csv_path)
        if df.empty:
            return {}
        return df.iloc[-1].to_dict()
    except Exception as e:
        print(f"    [PARSE ERROR] {e}")
        return {}

def main():
    args = parse_args()
    acc_name = args.github_user or os.environ.get("GITHUB_REPOSITORY_OWNER") or "runner"

    print("=" * 80)
    print(f"🚀 STARTING GEANT4 RUNNER #{args.runner_id} (Account: {acc_name})")
    print(f"Primaries per job: {args.primaries:,}")
    print(f"Plan file: {args.plan_file}")
    print("=" * 80)

    sim_bin = args.sim_bin or ensure_compiled()
    print(f">>> Binary Geant4 san sang: {sim_bin}")

    # 1. Connect Google Drive and locate 02_Flux_Data & 03_Simulation_Outputs
    drive_service = None
    flux_folder_id = None
    outputs_folder_id = None
    existing_files = set()

    try:
        drive_service = gdrive_helper.get_drive_service()
        subfolders = gdrive_helper.get_or_create_subfolders(drive_service, parent_id=args.gdrive_folder)
        flux_folder_id = subfolders.get("02_Flux_Data")
        outputs_folder_id = subfolders.get("03_Simulation_Outputs")

        print(f">>> [DRIVE] Ket noi Google Drive thanh cong!")
        print(f"  • Flat Common Pool (02_Flux_Data):      {flux_folder_id}")
        print(f"  • Raw Archive      (03_Simulation_Outputs): {outputs_folder_id}")

        # Scan existing files in Flat Common Pool
        q = f"'{flux_folder_id}' in parents and trashed = false"
        res_files = drive_service.files().list(
            q=q, supportsAllDrives=True, includeItemsFromAllDrives=True, fields="files(name)"
        ).execute()
        existing_files = {f["name"].lower().strip() for f in res_files.get("files", [])}
        print(f">>> [DRIVE] Tim thay {len(existing_files)} files da ton tai trong Flat Common Pool.")
    except Exception as e:
        print(f">>> [WARNING] Ket noi Drive that bai: {e}. Du lieu se duoc luu local.")

    # 2. Load execution plan
    with open(args.plan_file, "r", encoding="utf-8") as pf:
        full_plan = json.load(pf)

    matched_acc = None
    for acc in full_plan:
        if acc.lower() == acc_name.lower():
            matched_acc = acc
            break
    if not matched_acc:
        matched_acc = list(full_plan.keys())[0]

    runner_jobs = full_plan[matched_acc].get(str(args.runner_id), [])
    total_jobs = len(runner_jobs)
    print(f"\nRunner #{args.runner_id} duoc phan cong {total_jobs} jobs tu account '{matched_acc}'.")

    # 3. Execute Jobs
    proc_csv = Path("process_results.csv")
    generated_roots = []

    for idx, job in enumerate(runner_jobs, 1):
        jname = job["job_name"]
        sample = job["sample"]
        th_cm = job["thickness_cm"]
        e_mev = job["energy_mev"]
        macro_name = job["macro_file"]

        # Check candidate filenames (Flat Common Pool: S1_0.04cm_15keV.csv, etc.)
        candidates = job_naming.get_candidate_filenames(sample, th_cm, e_mev)
        candidates.append(f"{macro_name.replace('.mac', '')}.csv")
        candidates.append(f"{jname}.csv")

        if any(c.lower().strip() in existing_files for c in candidates):
            print(f"[{idx}/{total_jobs}] SKIP: [{jname}] da co trong Flat Common Pool tren Drive.")
            continue

        # Locate macro file
        macro_candidates = [
            Path("macros") / macro_name,
            Path("macros") / "macro" / macro_name,
            Path("build") / "macros" / macro_name,
            Path(macro_name)
        ]
        macro_path = None
        for mc in macro_candidates:
            if mc.exists():
                macro_path = mc
                break

        if not macro_path:
            print(f"[{idx}/{total_jobs}] ERR: Khong tim thay macro file {macro_name}!")
            continue

        print(f"\n--- [{idx}/{total_jobs}] RUNNING: {jname} ({macro_name}) ---")
        print(f"    Sample: {sample} | E = {job['energy_kev']:.1f} keV | Thickness = {job['thickness_mm']:.3f} mm")

        # Clean old run results
        if proc_csv.exists():
            try:
                proc_csv.unlink()
            except Exception:
                pass

        t0 = time.time()
        cmd = [sim_bin, str(macro_path.resolve())]
        res = subprocess.run(cmd, capture_output=True, text=True)
        elapsed = time.time() - t0

        if res.returncode != 0:
            print(f"    [SIM ERROR] code {res.returncode}: {(res.stderr or res.stdout)[-300:]}")
        else:
            print(f"    [SIM OK] Thoi gian chay: {elapsed:.2f}s")

        # Parse results
        last_row = parse_process_results_last_row(proc_csv)
        n_uncoll = float(last_row.get("N_uncollided", 0.0))
        frac_uncoll = float(last_row.get("Frac_uncollided", 0.0))
        mu_measured = float(last_row.get("Mu_measured_percm", 0.0))
        pulse_nai = float(last_row.get("Pulse_NaI", 0.0))
        edep = float(last_row.get("Edep_MeV", 0.0))
        n_tot = float(last_row.get("N_total", 0.0))

        # Format 1-to-1 standard CSV
        out_csv_name = f"{jname}.csv"
        rec = {
            "job_name": jname,
            "macro_file": macro_name,
            "sample": sample,
            "thickness_mm": job["thickness_mm"],
            "thickness_cm": th_cm,
            "energy_kev": job["energy_kev"],
            "energy_mev": e_mev,
            "peak_flux": n_uncoll,
            "tot_flux": pulse_nai,
            "frac_uncollided": frac_uncoll,
            "mu_measured_percm": mu_measured,
            "edep_mev": edep,
            "n_total": n_tot,
            "n_events": last_row.get("N_events", args.primaries),
            "n_compton": last_row.get("N_compton", 0),
            "n_photo": last_row.get("N_photo", 0),
            "n_pair": last_row.get("N_pair", 0),
            "n_rayleigh": last_row.get("N_rayleigh", 0),
            "r_compton": last_row.get("R_compton", 0),
            "r_photo": last_row.get("R_photo", 0),
            "r_pair": last_row.get("R_pair", 0),
            "r_rayleigh": last_row.get("R_rayleigh", 0),
            "sim_seconds": round(elapsed, 2)
        }
        df_out = pd.DataFrame([rec])
        df_out.to_csv(out_csv_name, index=False)

        # Upload directly to Flat Common Pool (02_Flux_Data)
        if drive_service and flux_folder_id:
            try:
                gdrive_helper.upload_file_to_folder(drive_service, out_csv_name, flux_folder_id)
                existing_files.add(out_csv_name.lower())
                print(f"    ✅ Uploaded {out_csv_name} -> 02_Flux_Data (Flat Common Pool)")
            except Exception as e:
                print(f"    ⚠️ Loi upload {out_csv_name}: {e}")

        # Check for root output
        root_cand = Path(job.get("root_file", ""))
        if not root_cand.exists():
            matches = list(Path.cwd().glob("hist_*.root"))
            if matches:
                root_cand = matches[0]

        if root_cand.exists():
            generated_roots.append(root_cand)

    # 4. Pack and upload child outputs to 03_Simulation_Outputs
    if generated_roots and drive_service and outputs_folder_id:
        zip_name = f"outputs_{acc_name}_runner_{args.runner_id:02d}.zip"
        print(f"\n>>> [ZIP] Dong goi {len(generated_roots)} root files vao {zip_name}...")
        with zipfile.ZipFile(zip_name, "w", zipfile.ZIP_DEFLATED) as z:
            for rf in generated_roots:
                z.write(rf, arcname=rf.name)
        gdrive_helper.upload_file_to_folder(drive_service, zip_name, outputs_folder_id)
        print(f"    ✅ Uploaded {zip_name} -> 03_Simulation_Outputs")

    print("\n" + "=" * 80)
    print(f"🎉 RUNNER #{args.runner_id} HOAN TAT MO PHONG!")
    print("=" * 80)

if __name__ == "__main__":
    main()
