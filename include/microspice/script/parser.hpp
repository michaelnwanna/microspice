#pragma once

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "microspice/script/quantity.hpp"

namespace microspice::script {

struct Token {
    std::string text;
    Span span;
    enum Kind { identifier, number, string, punctuation, newline, end } kind;
};

inline bool identifierStart(char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') || character == '_';
}
inline bool digit(char character) {
    return character >= '0' && character <= '9';
}

inline std::vector<Token> lex(const std::string& source, const Limits& limits,
                              const ExecutionControl& execution) {
    require(source.size() <= limits.sourceBytes, {},
            "source byte limit exceeded");
    std::vector<Token> tokens;
    std::size_t offset = 0, line = 1, column = 1;
    auto advance = [&]() {
        if (offset % 1024 == 0) execution.check();
        if (source[offset] == '\n') {
            ++line;
            column = 1;
        } else
            ++column;
        ++offset;
    };
    while (offset < source.size()) {
        execution.check();
        char character = source[offset];
        if (character == ' ' || character == '\t' || character == '\r') {
            advance();
            continue;
        }
        if (character == '#') {
            while (offset < source.size() && source[offset] != '\n') advance();
            continue;
        }
        Span span{offset, 1, line, column};
        Token::Kind kind = Token::punctuation;
        std::string text;
        if (character == '\n') {
            kind = Token::newline;
            text = "\n";
            advance();
        } else if (identifierStart(character)) {
            kind = Token::identifier;
            while (offset < source.size() &&
                   (identifierStart(source[offset]) || digit(source[offset]))) {
                text += source[offset];
                advance();
            }
        } else if (digit(character) ||
                   (character == '.' && offset + 1 < source.size() &&
                    digit(source[offset + 1]))) {
            kind = Token::number;
            while (offset < source.size() && digit(source[offset])) {
                text += source[offset];
                advance();
            }
            if (offset < source.size() && source[offset] == '.') {
                text += source[offset];
                advance();
                while (offset < source.size() && digit(source[offset])) {
                    text += source[offset];
                    advance();
                }
            }
            if (offset < source.size() &&
                (source[offset] == 'e' || source[offset] == 'E')) {
                text += source[offset];
                advance();
                if (offset < source.size() &&
                    (source[offset] == '+' || source[offset] == '-')) {
                    text += source[offset];
                    advance();
                }
                require(offset < source.size() && digit(source[offset]), span,
                        "scientific exponent requires digits");
                while (offset < source.size() && digit(source[offset])) {
                    text += source[offset];
                    advance();
                }
            }
        } else if (character == '"') {
            kind = Token::string;
            advance();
            while (offset < source.size() && source[offset] != '"') {
                require(source[offset] != '\n', span, "unterminated string");
                if (source[offset] == '\\') {
                    advance();
                    require(
                        offset < source.size() &&
                            (source[offset] == '\\' || source[offset] == '"'),
                        span, "only escaped quote and backslash are supported");
                }
                require(static_cast<unsigned char>(source[offset]) >= 32 &&
                            static_cast<unsigned char>(source[offset]) < 127,
                        span, "strings must contain printable ASCII");
                text += source[offset];
                advance();
            }
            require(offset < source.size(), span, "unterminated string");
            advance();
        } else {
            require(std::string("(),{}=+-*/<>!").find(character) !=
                        std::string::npos,
                    span, "unexpected character");
            text += character;
            advance();
            if (offset < source.size() && source[offset] == '=' &&
                std::string("=<>!").find(character) != std::string::npos) {
                text += '=';
                advance();
            }
        }
        span.length = offset - span.offset;
        tokens.push_back({std::move(text), span, kind});
    }
    tokens.push_back({"", {offset, 0, line, column}, Token::end});
    return tokens;
}

struct Expression;
using Expr = std::shared_ptr<Expression>;
struct Argument {
    std::string name;
    Expr value;
};
struct Expression {
    enum Kind { quantity, identifier, string, call, binary, unary } kind;
    Span span;
    std::string text;
    Quantity number;
    std::vector<Argument> arguments;
    std::size_t height = 1;
};
struct Statement {
    enum Kind { parameter, invocation, subcircuit } kind;
    Span span;
    std::string name;
    Expr value;
    std::vector<Argument> formals;
    std::vector<Statement> body;
};

class Parser {
    std::vector<Token> tokens_;
    std::size_t position_ = 0;
    Limits limits_;
    ExecutionControl execution_;
    const Token& peek() const { return tokens_[position_]; }
    Token take() {
        execution_.check();
        return tokens_[position_++];
    }
    bool accept(const std::string& text) {
        if (peek().text != text || (peek().kind != Token::punctuation &&
                                    peek().kind != Token::identifier))
            return false;
        take();
        return true;
    }
    Token expect(const std::string& text) {
        require(peek().text == text && peek().kind == Token::punctuation,
                peek().span, "expected '" + text + "'");
        return take();
    }
    void newlines() {
        while (peek().kind == Token::newline) take();
    }
    Token identifier() {
        require(peek().kind == Token::identifier, peek().span,
                "expected identifier");
        return take();
    }
    static int precedence(const std::string& text) {
        if (text == "<" || text == "<=" || text == ">" || text == ">=" ||
            text == "==" || text == "!=")
            return 1;
        if (text == "+" || text == "-") return 2;
        if (text == "*" || text == "/") return 3;
        return 0;
    }
    Expr expression(std::size_t depth = 1, int minimum = 1,
                    bool multiline = false) {
        execution_.check();
        require(depth <= limits_.nesting, peek().span,
                "expression nesting limit exceeded");
        if (multiline) newlines();
        Token token = take();
        auto result = std::make_shared<Expression>();
        result->span = token.span;
        result->text = token.text;
        if (token.kind == Token::punctuation &&
            (token.text == "+" || token.text == "-")) {
            result->kind = Expression::unary;
            result->arguments.push_back(
                {"", expression(depth + 1, 4, multiline)});
        } else if (token.kind == Token::punctuation && token.text == "(") {
            result = expression(depth + 1, 1, true);
            newlines();
            expect(")");
        } else if (token.kind == Token::number) {
            result->kind = Expression::quantity;
            double value = 0;
            try {
                value = std::stod(token.text);
            } catch (const std::exception&) {
                throw Error(token.span, "numeric literal out of range");
            }
            std::string unit;
            if (peek().kind == Token::identifier) unit = take().text;
            result->number = literal(value, unit, token.span);
        } else if (token.kind == Token::string)
            result->kind = Expression::string;
        else if (token.kind == Token::identifier) {
            result->kind = Expression::identifier;
            if (accept("(")) {
                result->kind = Expression::call;
                newlines();
                bool named = false;
                std::set<std::string> names;
                if (peek().kind != Token::punctuation || peek().text != ")")
                    do {
                        newlines();
                        std::string name;
                        if (peek().kind == Token::identifier &&
                            tokens_[position_ + 1].text == "=") {
                            name = take().text;
                            take();
                            named = true;
                            require(names.insert(name).second, peek().span,
                                    "duplicate named argument '" + name + "'");
                        } else
                            require(!named, peek().span,
                                    "positional argument after named argument");
                        result->arguments.push_back(
                            {name, expression(depth + 1, 1, true)});
                        newlines();
                    } while (accept(","));
                expect(")");
            }
        } else
            throw Error(token.span, "expected expression");
        for (const auto& argument : result->arguments)
            result->height =
                std::max(result->height, argument.value->height + 1);
        require(result->height <= limits_.nesting, result->span,
                "expression nesting limit exceeded");
        while (true) {
            if (multiline) newlines();
            int priority =
                peek().kind == Token::punctuation ? precedence(peek().text) : 0;
            if (priority < minimum) break;
            Token operation = take();
            auto combined = std::make_shared<Expression>();
            combined->kind = Expression::binary;
            combined->span = operation.span;
            combined->text = operation.text;
            combined->arguments = {
                {"", result},
                {"", expression(depth + 1, priority + 1, multiline)}};
            combined->height = std::max(combined->arguments[0].value->height,
                                        combined->arguments[1].value->height) +
                               1;
            require(combined->height <= limits_.nesting, operation.span,
                    "expression nesting limit exceeded");
            result = combined;
        }
        return result;
    }
    std::vector<Statement> statements(bool body, std::size_t depth) {
        require(depth <= limits_.nesting, peek().span,
                "template nesting limit exceeded");
        std::vector<Statement> result;
        newlines();
        while (peek().kind != Token::end && !(body && peek().text == "}")) {
            Statement statement{};
            statement.span = peek().span;
            if (accept("param")) {
                statement.kind = Statement::parameter;
                statement.name = identifier().text;
                expect("=");
                statement.value = expression();
            } else if (accept("subcircuit")) {
                require(!body, statement.span,
                        "nested subcircuit definitions are not allowed");
                statement.kind = Statement::subcircuit;
                statement.name = identifier().text;
                expect("(");
                newlines();
                if (peek().text != ")") do {
                        newlines();
                        auto formal = identifier();
                        Expr value;
                        if (accept("=")) value = expression(1, 1, true);
                        statement.formals.push_back({formal.text, value});
                        newlines();
                    } while (accept(","));
                expect(")");
                newlines();
                expect("{");
                statement.body = statements(true, depth + 1);
                expect("}");
            } else {
                statement.kind = Statement::invocation;
                statement.value = expression();
                require(statement.value->kind == Expression::call,
                        statement.span, "expected declarative function call");
            }
            result.push_back(std::move(statement));
            require(peek().kind == Token::newline ||
                        peek().kind == Token::end ||
                        (body && peek().text == "}"),
                    peek().span, "expected end of statement");
            newlines();
        }
        return result;
    }

public:
    Parser(const std::string& source, Limits limits = {},
           ExecutionControl execution = {})
        : tokens_(lex(source, limits, execution)),
          limits_(limits),
          execution_(std::move(execution)) {}
    std::vector<Statement> parse() {
        auto result = statements(false, 1);
        require(peek().kind == Token::end, peek().span,
                "unexpected closing brace");
        return result;
    }
};

}  // namespace microspice::script