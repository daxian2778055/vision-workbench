#!/usr/bin/env python3
"""Self-test for tools/scan_i18n_surface.py: does the stated definition actually classify?

Why this exists: the scanner is the source docs/对标差距推进计划.md row C3 quotes its numbers
from, and a source-shape count is only worth anything if the shape -> bucket rule is pinned by
something executable. Without this file, "812 widget_text / 372 data_identity" is a number that
changes meaning whenever someone edits UI_CALLS or a statement-head regex -- and the edit that
changes it looks like a refactor.

Run:  python tools/selftest_scan_i18n_surface.py
Exit code: 0 when every shape matches, 1 otherwise.

Shapes (one synthetic .cpp, one assert per line; the fixture never touches repo sources):
  wrapped-tr        tr("...")                        -> wrapped, not bare
  ui-setter         setText(QStringLiteral("..."))   -> setText / widget_text
  ui-ctor           new QLabel("...")                -> QLabel / widget_text
  param-label       makeDoubleParam(..., "起点行")   -> makeDoubleParam / param_label
  identity-name     node->setName("角度测量")        -> setName / data_identity
  identity-compare  if (name == "角度测量")          -> if / data_identity
  port-name         addOutputPort("测量结果")        -> addOutputPort / unresolved. Port names
                    are written into the scheme but never read back, so they are not a proven
                    lookup key; pinning the bucket is what stops a future edit from quietly
                    promoting them into the "must not wrap" tier (where they first sat).
  assign-error      error = QStringLiteral("...")    -> assign:error / runtime_message
  assign-plus       error += "；保存失败"            -> assign:error / runtime_message
  return-switch     case X: return QStringLiteral()   -> return / unresolved
  stream-debug      VFP_DEBUG << "..."               -> stream:VFP_DEBUG / not_ui_text
  escaped-cjk       fromUtf8("\\u786e\\u5b9a \\u00e9")  -> counted by the escaped-CJK measure (2 of
                    the 3 escapes -- the non-CJK one must be filtered), and it must NOT join the
                    bare list: those code points reach the screen once the compiler decodes them,
                    but the `literal` rule only sees ASCII
  raw-html          QStringLiteral(R"( ... )")-> NOT bare, counted as raw CJK lines; the
                    body holds a quoted 中文 fragment ("微软雅黑"), the exact shape that
                    produced 3 phantom bare literals in the first version

The raw body is the negative control: text that must NOT appear as a bare literal, because
lupdate cannot extract a phrase from inside it and it is reported as a blind spot instead. If
it shows up in the bare list, the masking rule has broken -- which is exactly what the first
version of the scanner did, counting 3 literals out of one HTML template.

(ui/*.ui is out of scope for the same kind of reason and is not asserted here: uic emits those
strings inside retranslateUi() already wrapped in tr().)
"""
import importlib.util
import io
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def load_scanner():
    spec = importlib.util.spec_from_file_location(
        "scan_i18n_surface", os.path.join(HERE, "scan_i18n_surface.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


FIXTURE = u"""\
#include <QLabel>
void f(Widget *w, Node *node, QString &error)
{
    tr("取消");
    w->setText(QStringLiteral("确定"));
    QLabel *l = new QLabel("标题");
    params.append(makeDoubleParam("r1a", 0, 0, 100, "线段1起点列", "px"));
    node->setName(QStringLiteral("角度测量"));
    node->addOutputPort(QStringLiteral("测量结果"));
    if (name == QStringLiteral("角度测量")) { g(); }
    error = QStringLiteral("打开失败");
    error += QStringLiteral("；保存失败");
    switch (t) {
    case RuntimeControlType::Button: return QStringLiteral("按钮");
    }
    VFP_DEBUG << "调试输出";
    w->setText(QString::fromUtf8("\\u786e\\u5b9a \\u00e9"));
    QString html = QStringLiteral(R"(
<p>段落</p>
<style>font-family: "微软雅黑";</style>
)");
}
"""

# (id, substring that must appear as a bare literal's key, expected key, expected tier)
EXPECTED = [
    ("ui-setter", u"确定", "setText", "widget_text"),
    ("ui-ctor", u"标题", "QLabel", "widget_text"),
    ("param-label", u"线段1起点列", "makeDoubleParam", "param_label"),
    ("identity-name", u"角度测量", "setName", "data_identity"),
    ("identity-compare", u"角度测量", "if", "data_identity"),
    ("port-name", u"测量结果", "addOutputPort", "unresolved"),
    ("assign-error", u"打开失败", "assign:error", "runtime_message"),
    ("assign-plus", u"；保存失败", "assign:error", "runtime_message"),
    ("return-switch", u"按钮", "return", "unresolved"),
    ("stream-debug", u"调试输出", "stream:VFP_DEBUG", "not_ui_text"),
]

# literals that must never be counted as bare
MUST_NOT_BE_BARE = [u"段落", u"微软雅黑", u"取消"]


def main():
    # Case lines print Chinese previews; a cp936 console mangles them without this.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    mod = load_scanner()
    tmp = tempfile.mkdtemp(prefix="i18n_fixture_")
    bad = 0
    try:
        path = os.path.join(tmp, "fixture.cpp")
        with io.open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(FIXTURE)
        wrapped, bare, raw_count, raw_cjk, escaped = mod.scan_file(path)

        def find(text):
            return [(key, line) for key, line, preview in bare if preview == text[:24]]

        for cid, text, want_key, want_tier in EXPECTED:
            hits = find(text)
            keys = {k for k, _ in hits}
            tier = mod.tier_of(want_key)
            ok = (want_key in keys and tier == want_tier)
            if not ok:
                bad += 1
            print("%-16s %-14s key=%-30s tier=%-15s %s"
                  % (cid, repr(text)[1:-1], ",".join(sorted(keys)) or "(none)", tier,
                     "OK" if ok else "MISMATCH want key=%s tier=%s" % (want_key, want_tier)))

        for text in MUST_NOT_BE_BARE:
            hits = find(text)
            if hits:
                bad += 1
            print("%-16s %-14s bare=%s  %s"
                  % ("negative-control", repr(text)[1:-1], hits,
                     "OK" if not hits else "MISMATCH (must not be bare)"))

        checks = [
            ("bare-total-exact", len(bare) == 10,
             "bare=%d want 10 (a leak out of the raw body makes this 11)" % len(bare)),
            ("wrapped-tr", wrapped == 1, "wrapped=%d want 1" % wrapped),
            ("raw-masked", raw_count == 1 and raw_cjk == 2,
             "raw=%d raw_cjk_lines=%d want 1/2" % (raw_count, raw_cjk)),
            ("escaped-cjk", escaped == 2,
             "escaped=%d want 2 of 3 (the non-CJK one is filtered; bare-total-exact "
             "above pins that none of them is a literal)" % escaped),
        ]
        for cid, ok, detail in checks:
            if not ok:
                bad += 1
            print("%-16s %s %s" % (cid, "OK" if ok else "MISMATCH", detail))

        print("I18N-SCAN-FIXTURE cases=%d mismatch=%d"
              % (len(EXPECTED) + len(MUST_NOT_BE_BARE) + len(checks), bad))
        return 1 if bad else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
