"""Extract the production startup/diagnostic branches for hidden-window tests."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
window = (root / 'src/Magpie.Core/ScalingWindow.cpp').read_text(encoding='utf-8-sig')


def block(text, marker):
    start = text.index(marker)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


startup = block(window, 'ScalingError ScalingWindow::_StartImpl(')
tail = startup[startup.index('\t_renderer = std::make_unique<class Renderer>();'):]
assert 'rendererInitializationFailed = false;' in startup
assert startup.index('_renderer->Initialize(') < startup.index('TrySetScalingWindowOwner(')
# Both real output modes remain unowned during initialization.
assert startup.count('\n\t\t\tnullptr,\n\t\t\tNULL,') == 2
destroy = block(window, 'case WM_DESTROY:')
assert destroy.index('TrySetScalingWindowOwner(Handle(), nullptr)') < destroy.index('BeginShutdown()')
assert destroy.index('TrySetScalingWindowOwner(Handle(), nullptr)') < destroy.index('_renderer.reset()')
assert 'NgxRuntimeGuard::IsFaulted()' in destroy
start = block(window, 'void ScalingWindow::Start(')
report = start[start.index('\tLogger::DiagnosticCapture startupDiagnostic;'):]
describe = block(window, 'static std::string DescribeScalingWindowOwner(')
creation_checks = (
    'ScalingError CheckOutputCreated() {\n' + block(startup, 'if (!Handle())')
    + '\nreturn ScalingError::NoError;\n}\n'
    + 'ScalingError CheckChildCreated() {\n' + block(startup, 'if (!_hwndRenderer)')
    + '\nreturn ScalingError::NoError;\n}\n')
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
(out / 'ScalingWindowOwnerStartup.inc').write_text(
    creation_checks + describe + '\nScalingError _StartImpl(HWND hwndSrc, bool& rendererInitializationFailed) noexcept {\n'
    '\trendererInitializationFailed = false;\n' + tail + '\nvoid Start(HWND hwndSrc) noexcept {\n'
    + report, encoding='utf-8')
print('Extracted production startup, error reporting, owner diagnostics; lifecycle order passed.')
