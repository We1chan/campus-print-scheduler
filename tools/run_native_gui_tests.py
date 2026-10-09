"""Run native acceptance and publish its text even for a Windows GUI binary."""
import argparse
import os
from pathlib import Path
import subprocess
import sys


def main():
    # Windows CI redirects stdout and may choose cp1252 despite UTF-8 reports.
    # Keep Chinese diagnostics intact rather than failing while printing them.
    sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin-dir", type=Path, required=True)
    parser.add_argument("--log-dir", type=Path, default=Path("runtime"))
    args = parser.parse_args()
    args.log_dir.mkdir(parents=True, exist_ok=True)
    binaries = args.bin_dir.resolve()
    suffix = ".exe" if sys.platform == "win32" else ""
    scenarios = [("gui_tests", "native-gui-tests.txt", [
        "officeDocumentsRealPreview", "officeStudentPreviewAndReplacement",
        "previewPreservesMixedPageDirections", "editorialMotionRetargeting",
        "editorialAnimationFrameBudget", "editorialReceiptCodes",
        "editorialHoverSpreadsAndReturns", "editorialWindowChrome",
        "editorialDocumentPreview", "studentOrderPaymentAndForecast",
        "persistentPoliciesAndReport", "backendCrashDuringCancellation",
        "runningCancelFaultAndReconnect", "compactWindowKeepsControlsReachable"])]
    if sys.platform == "win32" and (binaries / "merchant_tests.exe").is_file():
        scenarios.append(("merchant_tests", "native-merchant-tests.txt", [
            "nativeDispatchWorkspaceIsInteractiveAndIsolated",
            "nativeWindowControlsPreserveMouseFocusAndRestore",
            "nativeShopMenuKeepsBusinessActionsAndReturns",
            "persistentBackendReceivesOtherClientOrder"]))
    environment = dict(os.environ)
    environment["QT_QPA_PLATFORM"] = "windows" if sys.platform == "win32" else "cocoa"
    evidence = args.log_dir.resolve() / "native-gui-evidence"
    environment["CPS_GUI_EVIDENCE_DIR"] = str(evidence)
    environment["CPS_MERCHANT_EVIDENCE_DIR"] = str(evidence / "merchant")
    failed = False
    for binary, filename, cases in scenarios:
        report = (args.log_dir / filename).resolve()
        result = subprocess.run([str(binaries / (binary + suffix)), *cases,
                                 "-o", str(report) + ",txt"], env=environment, timeout=300)
        print(report.read_text(encoding="utf-8", errors="replace"), flush=True)
        failed = failed or result.returncode != 0
    if failed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
