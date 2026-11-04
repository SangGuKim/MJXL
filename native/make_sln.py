"""VS2026 CMake 솔루션을 기존 .sln 형식으로도 제공한다."""
from pathlib import Path, PureWindowsPath
import sys
import xml.etree.ElementTree as ET

root = Path(sys.argv[1]).resolve()
project_root = Path(__file__).resolve().parent.parent
solution = ET.parse(root / 'mjxl.slnx')
projects = []
for node in solution.findall('.//Project'):
    projects.append({'path': node.attrib['Path'], 'guid': '{' + node.attrib['Id'].upper() + '}',
                     'name': PureWindowsPath(node.attrib['Path']).stem,
                     'type': '{' + node.attrib['Type'].upper() + '}',
                     'deps': [dep.attrib['Project'] for dep in node.findall('BuildDependency')],
                     'build': not any(build.attrib.get('Project') == 'false' for build in node.findall('Build'))})
projects.sort(key=lambda p: (p['name'] != 'mjxl', p['name']))
ids = {p['path']: p['guid'] for p in projects}

def write(destination):
    prefix = root.relative_to(destination.parent).as_posix().replace('/', '\\') if destination.parent != root else ''
    lines = ['Microsoft Visual Studio Solution File, Format Version 12.00',
             '# Visual Studio Version 18', 'VisualStudioVersion = 18.10.12224.181',
             'MinimumVisualStudioVersion = 10.0.40219.1']
    for p in projects:
        path = str(PureWindowsPath(prefix) / PureWindowsPath(p['path']))
        lines.append(f'Project("{p["type"]}") = "{p["name"]}", "{path}", "{p["guid"]}"')
        if p['deps']:
            lines.append('\tProjectSection(ProjectDependencies) = postProject')
            for dep in p['deps']:
                lines.append(f'\t\t{ids[dep]} = {ids[dep]}')
            lines.append('\tEndProjectSection')
        lines.append('EndProject')
    lines += ['Global', '\tGlobalSection(SolutionConfigurationPlatforms) = preSolution']
    for config in ['Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel']:
        lines.append(f'\t\t{config}|x64 = {config}|x64')
    lines += ['\tEndGlobalSection', '\tGlobalSection(ProjectConfigurationPlatforms) = postSolution']
    for p in projects:
        for config in ['Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel']:
            lines.append(f'\t\t{p["guid"]}.{config}|x64.ActiveCfg = {config}|x64')
            if p['build']:
                lines.append(f'\t\t{p["guid"]}.{config}|x64.Build.0 = {config}|x64')
    lines += ['\tEndGlobalSection', 'EndGlobal']
    destination.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
    print(destination)

write(root / 'mjxl.sln')
write(project_root / 'MJXL.sln')
