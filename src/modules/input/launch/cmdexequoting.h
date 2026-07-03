#ifndef CMDEXEQUOTING_H
#define CMDEXEQUOTING_H

// Leaf helper of the launch module. Lived in PathUtils historically, but its
// only consumer is LaunchManager's Windows batch-launcher spawn path, and the
// transform is command-line escaping, not path expansion/validation — so it
// sits next to that consumer (utils must not reach into modules; the reverse
// dependency is fine).

#include <QString>

namespace CmdExeQuoting {

/// Escapes a single argument so it survives the Windows `cmd.exe /c batch …`
/// parsing layer intact. A `.cmd`/`.bat` launcher is not an executable image,
/// so QProcess runs it through cmd.exe; the type-safe QStringList quoting Qt
/// applies is correct for the final CommandLineToArgvW pass but NOT for the
/// intervening cmd.exe interpreter, which still acts on `& ( ) < > | % ! ^ "`
/// even inside double quotes. A media filename or a `%collection%`-expanded
/// value carrying those characters (e.g. "Sonic & Knuckles (USA).rom") would
/// otherwise be mis-parsed by, or inject commands into, the batch interpreter.
///
/// Implements Daniel Colascione's canonical two-layer scheme
/// ("Everyone quotes command line arguments the wrong way", MSDN, 2011):
///   1. CommandLineToArgvW quoting — wrap in `"…"` and backslash-escape so the
///      child's CRT parses it back as exactly one argument.
///   2. cmd.exe caret-escaping — prefix every cmd metacharacter (including the
///      quotes added by step 1) with `^` so cmd strips the carets and forwards
///      the literal characters to the CommandLineToArgvW layer untouched.
/// The transform is pure (platform-independent string ops) so it is unit-tested
/// directly; whether it fully closes the cmd.exe gap on a live MSVC host is a
/// runtime-gated check left to manual MSVC-CI verification.
///
/// NOTE: a literal `%` cannot be fully neutralised from the command line —
/// caret-escaping suppresses cmd's metacharacter handling but variable
/// expansion (`%VAR%`) runs in an earlier phase. An unset `%name%` is passed
/// through verbatim; only a name matching a live environment variable would
/// expand. This residual is documented for the MSVC-CI confirmation step.
[[nodiscard]] QString quoteForCmdExe(const QString &arg);

} // namespace CmdExeQuoting

#endif // CMDEXEQUOTING_H
