#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include "kartlaunchertrust.h"

namespace kart {

namespace {

/// The Windows equivalent of the POSIX program roots below: an install under
/// "C:/Program Files" or "C:/Program Files (x86)" needs administrator rights to
/// write, so it earns the same quiet-but-still-disclosed treatment /usr/bin
/// gets.
///
/// Recognised by SHAPE, matched against the value the manifest carries rather
/// than an absolutised one. Both details are load-bearing:
///
///   • %ProgramFiles% is not readable off-platform, and a .kart authored on
///     Windows is routinely inspected on Linux. A shape test classifies the
///     same bundle the same way on either host.
///   • QFileInfo treats "C:/Program Files/…" as a RELATIVE path on POSIX and
///     prefixes the cwd, so the absolutised form never prefix-matches. This is
///     the mirror of the wrinkle trust_allowlistedLauncherIsStillDisclosed
///     documents for the POSIX roots on Windows.
///
/// Drive-letter agnostic and case-insensitive because Windows paths are, so a
/// D: install and "c:/program files/…" both match, and either separator is
/// accepted because the manifest carries whatever the authoring host wrote. A
/// ProgramFiles redirected somewhere non-standard stays loud — disclosure-first
/// means an unrecognised root costs a louder row, never a missing one.
///
/// Separators are folded and ".." is collapsed BEFORE the shape test, because
/// the prefix compare below gets that collapse for free from absolutisation and
/// this one would not: "C:/Program Files/../../Windows/System32/evil.exe"
/// prefixes an allowed root and lands outside it, which is exactly the escape
/// suspicious_dotDotTraversalEscapingRoot_flagged guards on the POSIX side.
/// Folding backslashes is safe to do unconditionally here: only a drive-letter
/// path can match, so a POSIX name containing a literal backslash is unaffected.
bool isWindowsProgramRootPath(const QString &path) {
  static const QRegularExpression kProgramFilesRoot(
      QStringLiteral(R"(^[A-Za-z]:/Program Files(?: \(x86\))?/.)"),
      QRegularExpression::CaseInsensitiveOption);
  QString normalized = path.trimmed();
  normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
  return kProgramFilesRoot.match(QDir::cleanPath(normalized)).hasMatch();
}

bool isPathInsideAllowedRoots(const QString &path) {
  if (isWindowsProgramRootPath(path)) return true;
  const QString home = QDir::homePath();
  const QStringList allowedRoots = {home, QStringLiteral("/usr/bin"),
                                    QStringLiteral("/usr/local/bin"), QStringLiteral("/opt")};
  const QString abs = QFileInfo(path).absoluteFilePath();
  for (const QString &root : allowedRoots) {
    if (abs.startsWith(root + QLatin1Char('/')) || abs == root) {
      return true;
    }
  }
  return false;
}

/// Lowercased executable name with any directory part and a trailing ".exe"
/// removed, so "/usr/bin/Python3", "python3" and "PYTHON3.EXE" compare equal.
QString executableBasename(const QString &path) {
  QString base = path.trimmed();
  const int slash = base.lastIndexOf(QLatin1Char('/'));
  const int backslash = base.lastIndexOf(QLatin1Char('\\'));
  const int cut = qMax(slash, backslash);
  if (cut >= 0) base = base.mid(cut + 1);
  base = base.toLower();
  if (base.endsWith(QLatin1String(".exe"))) base.chop(4);
  return base;
}

/// Programs whose arguments are themselves code, or that hand execution to a
/// program named in their arguments. Deliberately partial — see the
/// disclosure-first note in kartlaunchertrust.h: missing an entry here costs a
/// louder warning, never the warning itself.
bool isInterpreterProgram(const QString &path) {
  static const QSet<QString> kInterpreters = {
      // Shells.
      QStringLiteral("sh"), QStringLiteral("bash"), QStringLiteral("dash"), QStringLiteral("ash"),
      QStringLiteral("zsh"), QStringLiteral("ksh"), QStringLiteral("mksh"), QStringLiteral("fish"),
      QStringLiteral("csh"), QStringLiteral("tcsh"), QStringLiteral("busybox"),
      // Program launchers / environment and privilege wrappers: the program
      // that actually runs is named in the arguments.
      QStringLiteral("env"), QStringLiteral("xargs"), QStringLiteral("sudo"),
      QStringLiteral("doas"), QStringLiteral("su"), QStringLiteral("pkexec"),
      QStringLiteral("systemd-run"), QStringLiteral("nsenter"), QStringLiteral("setsid"),
      QStringLiteral("nohup"), QStringLiteral("timeout"), QStringLiteral("stdbuf"),
      // Scripting runtimes.
      QStringLiteral("python"), QStringLiteral("perl"), QStringLiteral("ruby"),
      QStringLiteral("node"), QStringLiteral("nodejs"), QStringLiteral("deno"),
      QStringLiteral("bun"), QStringLiteral("php"), QStringLiteral("lua"), QStringLiteral("luajit"),
      QStringLiteral("tclsh"), QStringLiteral("wish"), QStringLiteral("awk"),
      QStringLiteral("gawk"), QStringLiteral("mawk"), QStringLiteral("nawk"),
      QStringLiteral("osascript"), QStringLiteral("rscript"),
      // Windows equivalents — .kart bundles move between platforms.
      QStringLiteral("cmd"), QStringLiteral("powershell"), QStringLiteral("pwsh"),
      QStringLiteral("wscript"), QStringLiteral("cscript"), QStringLiteral("rundll32"),
      QStringLiteral("mshta"), QStringLiteral("regsvr32")};

  const QString base = executableBasename(path);
  if (base.isEmpty()) return false;
  if (kInterpreters.contains(base)) return true;
  // Versioned spellings: python3, python3.11, perl5.38, php8.2, lua5.4, ...
  static const QRegularExpression kVersioned(
      QStringLiteral("^(python|perl|ruby|php|lua|luajit|node|pwsh|tclsh)[0-9]+[0-9._-]*$"));
  return kVersioned.match(base).hasMatch();
}

/// Split a launch-parameter template the way a reader would: on whitespace,
/// with surrounding quotes stripped. This is a classification heuristic, not
/// the launch-time parser (LaunchCommandBuilder::parseParameters owns that) —
/// it only has to be good enough to spot a token worth shouting about.
QStringList looseParameterTokens(const QString &params) {
  static const QRegularExpression kWhitespace(QStringLiteral("\\s+"));
  QStringList out;
  const QStringList raw = params.split(kWhitespace, Qt::SkipEmptyParts);
  out.reserve(raw.size());
  for (QString token : raw) {
    if (token.size() >= 2 &&
        ((token.startsWith(QLatin1Char('"')) && token.endsWith(QLatin1Char('"'))) ||
         (token.startsWith(QLatin1Char('\'')) && token.endsWith(QLatin1Char('\''))))) {
      token = token.mid(1, token.size() - 2);
    } else {
      token.remove(QLatin1Char('"'));
      token.remove(QLatin1Char('\''));
    }
    if (!token.isEmpty()) out.append(token);
  }
  return out;
}

/// True when the parameters carry a flag whose documented job is to run an
/// inline string or a named program. As above: a heuristic that escalates,
/// never one that exonerates.
bool parametersCarryInlineCode(const QString &params) {
  static const QSet<QString> kExactFlags = {QStringLiteral("-c"),
                                            QStringLiteral("-e"),
                                            QStringLiteral("-exec"),
                                            QStringLiteral("--exec"),
                                            QStringLiteral("--eval"),
                                            QStringLiteral("--evaluate"),
                                            QStringLiteral("--command"),
                                            QStringLiteral("-command"),
                                            QStringLiteral("/c"),
                                            QStringLiteral("/k"),
                                            QStringLiteral("-enc"),
                                            QStringLiteral("-encodedcommand"),
                                            QStringLiteral("--encodedcommand")};
  static const QStringList kPrefixFlags = {QStringLiteral("--command="), QStringLiteral("--eval="),
                                           QStringLiteral("--exec="), QStringLiteral("-command="),
                                           QStringLiteral("--encodedcommand=")};

  const QStringList tokens = looseParameterTokens(params);
  for (const QString &token : tokens) {
    const QString lower = token.toLower();
    if (kExactFlags.contains(lower)) return true;
    for (const QString &prefix : kPrefixFlags) {
      if (lower.startsWith(prefix)) return true;
    }
  }
  return false;
}

/// Membership test for "resolves inside the extracted kart tree". An empty
/// root (pre-extraction) never matches.
class InTreeMatcher {
public:
  explicit InTreeMatcher(const QString &extractedRoot) {
    if (extractedRoot.isEmpty()) return;
    // Canonicalize the extraction root so symlinks/.. in either side compare
    // on equal footing. Fall back to the cleaned absolute path when the dir
    // doesn't exist yet (it always should post-extract, but be defensive).
    const QFileInfo rootInfo(extractedRoot);
    m_root = rootInfo.canonicalFilePath().isEmpty() ? QDir::cleanPath(rootInfo.absoluteFilePath())
                                                    : rootInfo.canonicalFilePath();
  }

  [[nodiscard]] bool contains(const QString &path) const {
    if (m_root.isEmpty() || path.isEmpty()) return false;
    const QFileInfo fi(path);
    // Resolve through symlinks when the target exists (a bundled payload
    // does); otherwise clean the absolute path so "<root>/../etc/x" can't
    // masquerade as in-tree and, conversely, "<root>/bin/x" is still detected
    // pre-launch.
    const QString canon = fi.canonicalFilePath();
    const QString abs = canon.isEmpty() ? QDir::cleanPath(fi.absoluteFilePath()) : canon;
    return abs == m_root || abs.startsWith(m_root + QLatin1Char('/'));
  }

  /// True when any path-shaped token of a parameter template lands in the
  /// tree — a bundled payload reached through an argument (`-L <root>/x.so`)
  /// rather than through corePath.
  [[nodiscard]] bool containsAnyToken(const QString &params) const {
    if (m_root.isEmpty()) return false;
    const QStringList tokens = looseParameterTokens(params);
    for (const QString &token : tokens) {
      if (!token.contains(QLatin1Char('/')) && !token.contains(QLatin1Char('\\'))) continue;
      if (contains(token)) return true;
    }
    return false;
  }

private:
  QString m_root;
};

} // namespace

bool isElevatedLauncherTrustReason(LauncherTrustReason reason) {
  return reason != LauncherTrustReason::BundleSupplied;
}

QString launcherTrustReasonLabel(LauncherTrustReason reason) {
  switch (reason) {
  case LauncherTrustReason::BundleSupplied:
    return QCoreApplication::translate("KartLauncherTrust", "Chosen by the bundle");
  case LauncherTrustReason::OutsideAllowlist:
    return QCoreApplication::translate("KartLauncherTrust", "Outside the safe allowlist");
  case LauncherTrustReason::InterpreterProgram:
    return QCoreApplication::translate("KartLauncherTrust", "Shell or interpreter");
  case LauncherTrustReason::InlineCodeParameter:
    return QCoreApplication::translate("KartLauncherTrust", "Runs an inline command");
  case LauncherTrustReason::BundledExecutable:
    return QCoreApplication::translate("KartLauncherTrust", "Shipped inside the bundle");
  }
  return QString{};
}

QList<KartLauncherFinding> collectLauncherTrustFindings(const CollectionConfig &cfg,
                                                        const QSet<QString> &trustedLauncherPaths,
                                                        const QString &extractedRoot) {
  QList<KartLauncherFinding> out;
  const InTreeMatcher inTree(extractedRoot);

  auto addLauncherPath = [&](const QString &field, const QString &path) {
    if (path.trimmed().isEmpty()) return;
    LauncherTrustReason reason = LauncherTrustReason::BundleSupplied;
    if (inTree.contains(path)) {
      reason = LauncherTrustReason::BundledExecutable;
    } else if (isInterpreterProgram(path)) {
      // Deliberately ahead of the allowlist and of the trusted-path exemption:
      // a program that executes its arguments is dangerous wherever it lives
      // and however often the user has approved that path before, because the
      // arguments arrive fresh with every bundle.
      reason = LauncherTrustReason::InterpreterProgram;
    } else if (!isPathInsideAllowedRoots(path) && !trustedLauncherPaths.contains(path)) {
      reason = LauncherTrustReason::OutsideAllowlist;
    }
    out.append({field, path, reason});
  };

  auto addCorePath = [&](const QString &field, const QString &path) {
    if (path.trimmed().isEmpty()) return;
    // No allowlist applies: cores legitimately live in library directories
    // (/usr/lib/..., ~/.config/.../cores) that a launcher-binary allowlist
    // was never meant to cover, so an allowlist here would flag every honest
    // bundle. A core is loaded into the launcher's own process, though, so a
    // bundle-shipped one is arbitrary code and needs no execute bit.
    out.append({field, path,
                inTree.contains(path) ? LauncherTrustReason::BundledExecutable
                                      : LauncherTrustReason::BundleSupplied});
  };

  auto addParameters = [&](const QString &field, const QString &params) {
    if (params.trimmed().isEmpty()) return;
    LauncherTrustReason reason = LauncherTrustReason::BundleSupplied;
    if (inTree.containsAnyToken(params)) {
      reason = LauncherTrustReason::BundledExecutable;
    } else if (parametersCarryInlineCode(params)) {
      reason = LauncherTrustReason::InlineCodeParameter;
    }
    out.append({field, params, reason});
  };

  const LauncherProfile &lp = cfg.launcher;
  addLauncherPath(QStringLiteral("launcher.launcherPath"), lp.launcherPath);
  addCorePath(QStringLiteral("launcher.corePath"), lp.corePath);
  addParameters(QStringLiteral("launcher.launchParameters"), lp.launchParameters);
  for (int i = 0; i < lp.additionalLaunchers.size(); ++i) {
    const LauncherConfig &alt = lp.additionalLaunchers.at(i);
    addLauncherPath(QStringLiteral("additionalLaunchers[%1].launcherPath").arg(i),
                    alt.launcherPath);
    addCorePath(QStringLiteral("additionalLaunchers[%1].corePath").arg(i), alt.corePath);
    addParameters(QStringLiteral("additionalLaunchers[%1].launchParameters").arg(i),
                  alt.launchParameters);
  }
  return out;
}

QList<SuspiciousKartPath> collectSuspiciousAssetPaths(const CollectionConfig &cfg) {
  QList<SuspiciousKartPath> out;
  auto check = [&](const QString &field, const QString &path) {
    if (path.isEmpty()) return;
    if (isPathInsideAllowedRoots(path)) return;
    out.append({field, path});
  };
  check(QStringLiteral("collectionIcon"), cfg.collectionIcon);
  check(QStringLiteral("placeholderArtwork"), cfg.placeholderArtwork);
  return out;
}

QList<SuspiciousKartPath> collectSuspiciousKartPaths(const CollectionConfig &cfg,
                                                     const QSet<QString> &trustedLauncherPaths) {
  QList<SuspiciousKartPath> out;
  auto checkLauncher = [&](const QString &field, const QString &path) {
    if (path.isEmpty()) return;
    if (isPathInsideAllowedRoots(path)) return;
    if (trustedLauncherPaths.contains(path)) return;
    out.append({field, path});
  };
  checkLauncher(QStringLiteral("launcher.launcherPath"), cfg.launcher.launcherPath);
  for (int i = 0; i < cfg.launcher.additionalLaunchers.size(); ++i) {
    checkLauncher(QStringLiteral("additionalLaunchers[%1].launcherPath").arg(i),
                  cfg.launcher.additionalLaunchers[i].launcherPath);
  }
  out.append(collectSuspiciousAssetPaths(cfg));
  return out;
}

QList<SuspiciousKartPath> collectInTreeLauncherPaths(const CollectionConfig &cfg,
                                                     const QString &extractedRoot) {
  QList<SuspiciousKartPath> out;
  if (extractedRoot.isEmpty()) return out;
  const InTreeMatcher inTree(extractedRoot);
  auto checkPath = [&](const QString &field, const QString &path) {
    if (inTree.contains(path)) out.append({field, path});
  };
  auto checkParams = [&](const QString &field, const QString &params) {
    if (inTree.containsAnyToken(params)) out.append({field, params});
  };
  const LauncherProfile &lp = cfg.launcher;
  checkPath(QStringLiteral("launcher.launcherPath"), lp.launcherPath);
  checkPath(QStringLiteral("launcher.corePath"), lp.corePath);
  checkParams(QStringLiteral("launcher.launchParameters"), lp.launchParameters);
  for (int i = 0; i < lp.additionalLaunchers.size(); ++i) {
    const LauncherConfig &alt = lp.additionalLaunchers.at(i);
    checkPath(QStringLiteral("additionalLaunchers[%1].launcherPath").arg(i), alt.launcherPath);
    checkPath(QStringLiteral("additionalLaunchers[%1].corePath").arg(i), alt.corePath);
    checkParams(QStringLiteral("additionalLaunchers[%1].launchParameters").arg(i),
                alt.launchParameters);
  }
  return out;
}

QList<SuspiciousKartPath>
importConfirmationRows(const QList<KartLauncherFinding> &findings,
                       const QList<SuspiciousKartPath> &suspiciousAssets) {
  QList<SuspiciousKartPath> rows;
  rows.reserve(findings.size() + suspiciousAssets.size());
  for (const KartLauncherFinding &f : findings) {
    rows.append(
        {QStringLiteral("%1 — %2").arg(f.field, launcherTrustReasonLabel(f.reason)), f.value});
  }
  for (const SuspiciousKartPath &asset : suspiciousAssets) {
    rows.append({QStringLiteral("%1 — %2").arg(
                     asset.first, launcherTrustReasonLabel(LauncherTrustReason::OutsideAllowlist)),
                 asset.second});
  }
  return rows;
}

} // namespace kart
