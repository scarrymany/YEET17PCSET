"""Build the YEET17PCSET MSI from a staged install layout.

Usage:
    python installer/build_msi.py --stage <dir> --version 1.0.2 --out <path.msi>

The stage dir is the same layout the release zip carries (YEET17PCSET.exe,
runtime DLLs, catalog/, presets/, resources/, src/). Requires the WiX CLI
(dotnet tool install --global wix).

The UpgradeCode is fixed forever: MajorUpgrade replaces any older version.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path
from xml.sax.saxutils import escape

UPGRADE_CODE = "8EADA5A6-9C59-4F96-B9D5-AC3CB31DDF20"
PRODUCT_NAME = "YEET17PCSET"
MANUFACTURER = "scarrymany"
EXE_NAME = "YEET17PCSET.exe"


def build_wxs(stage: Path, version: str) -> str:
    dir_lines: list[str] = []
    counters = {"d": 0, "f": 0, "c": 0}

    def next_id(kind: str) -> str:
        counters[kind] += 1
        return f"{kind}{counters[kind]}"

    def emit_dir(path: Path, indent: str) -> None:
        for child in sorted(path.iterdir(), key=lambda p: (p.is_file(), p.name.lower())):
            if child.is_dir():
                did = next_id("d")
                dir_lines.append(f'{indent}<Directory Id="{did}" Name="{escape(child.name)}">')
                emit_dir(child, indent + "  ")
                dir_lines.append(f"{indent}</Directory>")
            else:
                cid = next_id("c")
                fid = next_id("f")
                is_main_exe = child.name == EXE_NAME and path == stage
                dir_lines.append(f'{indent}<Component Id="{cid}" Bitness="always64">')
                dir_lines.append(
                    f'{indent}  <File Id="{fid}" Source="{escape(str(child))}" KeyPath="yes">')
                if is_main_exe:
                    dir_lines.append(
                        f'{indent}    <Shortcut Id="StartMenuShortcut" Directory="ProgramMenuFolder" '
                        f'Name="{PRODUCT_NAME}" WorkingDirectory="INSTALLFOLDER" '
                        f'Icon="AppIcon" Advertise="yes"/>')
                dir_lines.append(f"{indent}  </File>")
                dir_lines.append(f"{indent}</Component>")

    emit_dir(stage, "        ")
    files_xml = "\n".join(dir_lines)
    icon = stage / "resources" / "app.ico"

    return f"""<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">
  <Package Name="{PRODUCT_NAME}"
           Manufacturer="{MANUFACTURER}"
           Version="{version}"
           UpgradeCode="{UPGRADE_CODE}"
           Scope="perMachine"
           Compressed="yes">
    <MajorUpgrade DowngradeErrorMessage="Установлена более новая версия {PRODUCT_NAME}."/>
    <MediaTemplate EmbedCab="yes"/>
    <Icon Id="AppIcon" SourceFile="{escape(str(icon))}"/>
    <Property Id="ARPPRODUCTICON" Value="AppIcon"/>
    <Property Id="ARPURLINFOABOUT" Value="https://github.com/scarrymany/YEET17PCSET"/>
    <StandardDirectory Id="ProgramFiles64Folder">
      <Directory Id="INSTALLFOLDER" Name="{PRODUCT_NAME}">
{files_xml}
      </Directory>
    </StandardDirectory>
  </Package>
</Wix>
"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()

    stage = args.stage.resolve()
    if not (stage / EXE_NAME).is_file():
        print(f"error: {EXE_NAME} not found in {stage}", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        wxs = Path(tmp) / "Product.wxs"
        wxs.write_text(build_wxs(stage, args.version), encoding="utf-8")
        result = subprocess.run(
            ["wix", "build", "-arch", "x64", str(wxs), "-o", str(args.out.resolve())])
        if result.returncode != 0:
            return result.returncode
    print(f"built {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
