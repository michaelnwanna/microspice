#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

namespace microspice::gui {

class ScriptHighlighter final : public QSyntaxHighlighter {
public:
    explicit ScriptHighlighter(QTextDocument* document)
        : QSyntaxHighlighter(document) {}

protected:
    void highlightBlock(const QString& text) override {
        static const QRegularExpression tokens(
            R"msim(#[^\n]*|"(?:\\.|[^"\\])*"?|\b(?:param|subcircuit)\b|\b[A-Za-z_][A-Za-z_0-9]*(?=\s*\()|(?:\b\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?\s*(?:[A-Za-z]+)?)msim");
        auto matches = tokens.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            const auto token = match.captured();
            QTextCharFormat format;
            if (token.startsWith('#')) {
                format.setForeground(QColor("#63736b"));
                format.setFontItalic(true);
            } else if (token.startsWith('"')) {
                format.setForeground(QColor("#9b4a2b"));
            } else if (token == "param" || token == "subcircuit") {
                format.setForeground(QColor("#9a2955"));
                format.setFontWeight(QFont::Bold);
            } else if (token.front().isDigit() || token.startsWith('.')) {
                format.setForeground(QColor("#226796"));
            } else {
                format.setForeground(QColor("#007f78"));
                format.setFontWeight(QFont::DemiBold);
            }
            setFormat(static_cast<int>(match.capturedStart()),
                      static_cast<int>(match.capturedLength()), format);
        }
    }
};

}  // namespace microspice::gui