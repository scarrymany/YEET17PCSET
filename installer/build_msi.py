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

    exe_file_id: list[str] = []

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
                if child.name == EXE_NAME and path == stage:
                    exe_file_id.append(fid)
                dir_lines.append(f'{indent}<Component Id="{cid}" Bitness="always64">')
                dir_lines.append(
                    f'{indent}  <File Id="{fid}" Source="{escape(str(child))}" KeyPath="yes"/>')
                dir_lines.append(f"{indent}</Component>")

    emit_dir(stage, "        ")
    files_xml = "\n".join(dir_lines)
    icon = stage / "resources" / "app.ico"
    exe_ref = f"[#{exe_file_id[0]}]"

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
    <!-- Plain (non-advertised) shortcuts: their icon comes from the exe's own
         embedded icon, which advertised shortcuts failed to show. -->
    <StandardDirectory Id="ProgramMenuFolder">
      <Component Id="StartMenuShortcutComp" Bitness="always64">
        <Shortcut Id="StartMenuShortcut" Name="{PRODUCT_NAME}"
                  Target="{exe_ref}" WorkingDirectory="INSTALLFOLDER"/>
        <RegistryValue Root="HKLM" Key="Software\\{MANUFACTURER}\\{PRODUCT_NAME}"
                       Name="StartMenuShortcut" Value="1" Type="integer" KeyPath="yes"/>
      </Component>
    </StandardDirectory>
    <StandardDirectory Id="DesktopFolder">
      <Component Id="DesktopShortcutComp" Bitness="always64">
        <Shortcut Id="DesktopShortcut" Name="{PRODUCT_NAME}"
                  Target="{exe_ref}" WorkingDirectory="INSTALLFOLDER"/>
        <RegistryValue Root="HKLM" Key="Software\\{MANUFACTURER}\\{PRODUCT_NAME}"
                       Name="DesktopShortcut" Value="1" Type="integer" KeyPath="yes"/>
      </Component>
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
