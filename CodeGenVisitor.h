#pragma once

#include "CSubsetBaseVisitor.h"
#include "CGSymbol.h"
#include <sstream>
#include <string>
#include <vector>

// Phase 2 Intermediate Code Generator.
//
// Supported (full CSubset grammar, see P1_Grammar.pdf / the ICG spec):
//   - global & local int/void declarations (scalars and fixed-size arrays),
//     optionally initialized with a constant int literal
//   - function definitions with any number of parameters (int/void return)
//   - arithmetic (+ - * / %), unary (+ - !), relational (< <= > >= == !=),
//     logical (&& ||) with short-circuit evaluation
//   - assignment, compound assignment (+= -= *= /= %=), postfix ++/--,
//     println(args...), return expr;
//   - if / if-else / while / for, function calls, array indexing
//
// Every expression-generating visit leaves its result in EAX on exit,
// following the pattern demonstrated in fasm/tutorial.pdf and
// fasm/codes/*.asm (evaluate right side, PUSH, evaluate left side into
// EAX, POP right into EBX, operate EAX/EBX).

class CodeGenVisitor : public CSubsetBaseVisitor {
public:
    CodeGenVisitor(const std::vector<std::string>& sourceLines);

    std::ostringstream dataSeg;   // global variable storage
    std::ostringstream codeSeg;   // finished functions, one after another
    bool sawMain = false;

    std::any visitVar_declaration(CSubsetParser::Var_declarationContext* ctx) override;
    std::any visitDeclaration_list(CSubsetParser::Declaration_listContext* ctx) override;
    std::any visitFunc_definition(CSubsetParser::Func_definitionContext* ctx) override;
    std::any visitCompound_statement(CSubsetParser::Compound_statementContext* ctx) override;
    std::any visitStatement(CSubsetParser::StatementContext* ctx) override;
    std::any visitExpression(CSubsetParser::ExpressionContext* ctx) override;
    std::any visitLogic_expression(CSubsetParser::Logic_expressionContext* ctx) override;
    std::any visitRel_expression(CSubsetParser::Rel_expressionContext* ctx) override;
    std::any visitSimple_expression(CSubsetParser::Simple_expressionContext* ctx) override;
    std::any visitTerm(CSubsetParser::TermContext* ctx) override;
    std::any visitUnary_expression(CSubsetParser::Unary_expressionContext* ctx) override;
    std::any visitFactor(CSubsetParser::FactorContext* ctx) override;

private:
    const std::vector<std::string>& sourceLines;
    CGScope scope;

    bool insideFunction = false;
    std::string currentDeclType;
    std::string currentFuncLabel;
    bool currentIsMain = false;
    int currentLocalOffset = -4;
    int labelCounter = 0;
    std::ostringstream funcBodyBuffer; // one buffer per function; the "temporary" storage
                                        // for on-the-fly generation before the frame size
                                        // (SUB ESP, N) is known.

    static std::string typeOf(CSubsetParser::Type_specifierContext* ctx);
    std::string newLabelBase();
    void emit(const std::string& instr);
    void emitLabel(const std::string& label);
    void emitComment(const std::string& text);
    void annotateLine(int line);

    // Resolves a `variable` (plain ID or ID[expr]) to the asm operand text
    // usable directly in a MOV/PUSH/etc ("[var_x]", "[EBP-4]", "[g + EBX]",
    // "[EBP + ESI]"). For an array element this EMITS the index-computation
    // code (evaluates the index expression, clobbers EAX/EBX/ESI) as a side
    // effect, so callers that already hold a needed value in EAX must
    // PUSH/POP around the call. Returns "" (and emits a warning comment) if
    // the identifier can't be resolved.
    std::string addressOf(CSubsetParser::VariableContext* ctx);

    void genCompare(CSubsetParser::Simple_expressionContext* left,
                     CSubsetParser::Simple_expressionContext* right,
                     const std::string& op);
    void printArguments(CSubsetParser::ArgumentsContext* ctx);

    static void collectParams(CSubsetParser::Parameter_listContext* ctx, std::vector<ParamInfo>& out);
    static void collectArguments(CSubsetParser::ArgumentsContext* ctx,
                                  std::vector<CSubsetParser::Logic_expressionContext*>& out);
};
