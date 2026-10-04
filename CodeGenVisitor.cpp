#include "CodeGenVisitor.h"
#include <cctype>

CodeGenVisitor::CodeGenVisitor(const std::vector<std::string>& sourceLines)
    : sourceLines(sourceLines) {}

std::string CodeGenVisitor::typeOf(CSubsetParser::Type_specifierContext* ctx) {
    if (ctx->INT()) return "int";
    if (ctx->FLOAT()) return "float";
    return "void";
}

std::string CodeGenVisitor::newLabelBase() {
    return "L" + std::to_string(labelCounter++);
}

void CodeGenVisitor::emit(const std::string& instr) {
    funcBodyBuffer << "    " << instr << "\n";
}

void CodeGenVisitor::emitLabel(const std::string& label) {
    funcBodyBuffer << label << ":\n";
}

void CodeGenVisitor::emitComment(const std::string& text) {
    funcBodyBuffer << "    ; " << text << "\n";
}

void CodeGenVisitor::annotateLine(int line) {
    std::string src;
    if (line >= 1 && (size_t)line <= sourceLines.size()) {
        src = sourceLines[line - 1];
        size_t a = src.find_first_not_of(" \t");
        size_t b = src.find_last_not_of(" \t\r");
        src = (a == std::string::npos) ? "" : src.substr(a, b - a + 1);
    }
    funcBodyBuffer << "    ; Line " << line << ": " << src << "\n";
}

// ==================== variable -> asm operand ====================
std::string CodeGenVisitor::addressOf(CSubsetParser::VariableContext* ctx) {
    std::string name = ctx->ID()->getText();
    CGSymbol* sym = scope.lookup(name);

    if (ctx->expression() != nullptr) {
        // ID LTHIRD expression RTHIRD -- array element access.
        if (!sym || !sym->isArray) {
            emitComment("[warn] '" + name + "' used as an array but is not one");
            visit(ctx->expression());
            return "";
        }
        visit(ctx->expression());   // index -> EAX
        emit("MOV EBX, 4");
        emit("MUL EBX");            // turns index into byte offset EAX = byte offset (index * 4)
        if (sym->isGlobal) {
            emit("MOV EBX, EAX");
            return "[" + sym->label + " + EBX]";
        }
        emit("MOV EBX, " + std::to_string(sym->arrayBase));
        emit("ADD EAX, EBX");
        emit("MOV ESI, EAX");
        emit("NEG ESI");
        return "[EBP + ESI]"; // [ebp-N]
    }

    if (!sym) {
        emitComment("[warn] undeclared identifier '" + name + "'");
        return "";
    }
    return sym->operand;
}

// ==================== var_declaration ====================
std::any CodeGenVisitor::visitVar_declaration(CSubsetParser::Var_declarationContext* ctx) {
    currentDeclType = typeOf(ctx->type_specifier());
    if (currentDeclType == "float") {
        emitComment("[Phase 2] float variables not required/implemented in Phase 1");
    }
    visit(ctx->declaration_list());
    return {};
}

// ==================== declaration_list ====================
std::any CodeGenVisitor::visitDeclaration_list(CSubsetParser::Declaration_listContext* ctx) {
    if (ctx->declaration_list()) visit(ctx->declaration_list());
    std::string name = ctx->ID()->getText();
    // LTHIRD / ASSIGNOP only ever appear together with CONST_INT in mutually
    // exclusive alternatives (array-size vs. initializer), so check the
    // token that is unique to each alternative rather than CONST_INT itself.
    bool isArrayDecl = (ctx->LTHIRD() != nullptr);
    bool hasInit = (ctx->ASSIGNOP() != nullptr);

    if (insideFunction) {
        if (isArrayDecl) {
            int size = std::stoi(ctx->CONST_INT()->getText());
            int base = -currentLocalOffset; // magnitude of the first element's offset
            scope.declareLocalArray(name, currentDeclType, base, size);
            currentLocalOffset -= size * 4;
        } else {
            int offset = currentLocalOffset;
            currentLocalOffset -= 4;
            scope.declareLocal(name, currentDeclType, offset);
            if (hasInit) {
                emit("MOV EAX, " + ctx->CONST_INT()->getText());
                emit("MOV " + scope.lookup(name)->operand + ", EAX");
            }
        }
    } else {
        if (isArrayDecl) {
            int size = std::stoi(ctx->CONST_INT()->getText());
            scope.declareGlobalArray(name, currentDeclType, size);
            dataSeg << "    var_" << name << " dd " << size << " dup (0)\n";
        } else {
            scope.declareGlobal(name, currentDeclType);
            std::string initVal = hasInit ? ctx->CONST_INT()->getText() : "0";
            dataSeg << "    var_" << name << " dd " << initVal << "\n";
        }
    }
    return {};
}

// ==================== parameter_list ====================
void CodeGenVisitor::collectParams(CSubsetParser::Parameter_listContext* ctx, std::vector<ParamInfo>& out) {
    if (ctx->parameter_list()) collectParams(ctx->parameter_list(), out);
    ParamInfo p;
    p.type = typeOf(ctx->type_specifier());
    p.name = ctx->ID() ? ctx->ID()->getText() : "";
    out.push_back(p);
}

// ==================== func_definition ====================
std::any CodeGenVisitor::visitFunc_definition(CSubsetParser::Func_definitionContext* ctx) {
    std::string returnType = typeOf(ctx->type_specifier());
    std::string funcName = ctx->ID()->getText();
    bool isMain = (funcName == "main");
    std::string label = isMain ? "main" : ("func_" + funcName);

    std::vector<ParamInfo> params;
    if (ctx->parameter_list()) collectParams(ctx->parameter_list(), params);

    FuncInfo fi;
    fi.name = funcName;
    fi.label = label;
    fi.returnType = returnType;
    fi.params = params;
    scope.functions[funcName] = fi;

    bool savedInside = insideFunction;
    std::string savedLabel = currentFuncLabel;
    bool savedIsMain = currentIsMain;
    int savedOffset = currentLocalOffset;
    std::ostringstream savedBuffer;
    savedBuffer.swap(funcBodyBuffer);

    insideFunction = true;
    currentFuncLabel = label;
    currentIsMain = isMain;
    currentLocalOffset = -4;

    scope.enter(); // parameter scope (outer to the compound_statement's own scope)
    int paramOffset = 8;
    for (auto& p : params) {
        if (!p.name.empty()) scope.declareLocal(p.name, p.type, paramOffset);
        paramOffset += 4;
    }

    visit(ctx->compound_statement());

    scope.exit();

    int frameSize = (-currentLocalOffset) - 4; // total bytes reserved for locals
    int paramBytes = (int)params.size() * 4;

    codeSeg << label << ":\n"
            << "    PUSH EBP\n"
            << "    MOV  EBP, ESP\n"
            << "    SUB  ESP, " << frameSize << "\n"
            << funcBodyBuffer.str()
            << label << "_exit:\n"
            << "    ADD  ESP, " << frameSize << "\n"
            << "    POP  EBP\n";
    if (isMain) {
        codeSeg << "    MOV EAX, 1\n"
                << "    XOR EBX, EBX\n"
                << "    INT 0x80\n";
        sawMain = true;
    } else {
        codeSeg << "    RET" << (paramBytes > 0 ? (" " + std::to_string(paramBytes)) : "") << "\n";
    }
    codeSeg << "\n";

    funcBodyBuffer.swap(savedBuffer);
    insideFunction = savedInside;
    currentFuncLabel = savedLabel;
    currentIsMain = savedIsMain;
    currentLocalOffset = savedOffset;
    return {};
}

// ==================== compound_statement ====================
std::any CodeGenVisitor::visitCompound_statement(CSubsetParser::Compound_statementContext* ctx) {
    scope.enter();
    if (ctx->statements()) visit(ctx->statements());
    scope.exit();
    return {};
}

// ==================== statement ====================
std::any CodeGenVisitor::visitStatement(CSubsetParser::StatementContext* ctx) {
    int line = ctx->getStart()->getLine();

    if (ctx->var_declaration()) {
        annotateLine(line);
        visit(ctx->var_declaration());
        return {};
    }
    if (!ctx->expression_statement().empty() && !ctx->FOR()) {
        annotateLine(line);
        auto* es = ctx->expression_statement(0);
        if (es->expression()) visit(es->expression());
        return {};
    }
    if (ctx->compound_statement()) {
        visit(ctx->compound_statement());
        return {};
    }
    if (ctx->PRINTLN()) {
        annotateLine(line);
        printArguments(ctx->arguments());
        return {};
    }
    if (ctx->RETURN()) {
        annotateLine(line);
        visit(ctx->expression());
        emit("JMP " + (currentIsMain ? std::string("main_exit") : (currentFuncLabel + "_exit")));
        return {};
    }
    if (ctx->IF()) {
        annotateLine(line);
        std::string base = newLabelBase();
        visit(ctx->expression());
        emit("TEST EAX, EAX");
        if (ctx->ELSE()) {
            emit("JE " + base + "_else");
            visit(ctx->statement(0));
            emit("JMP " + base + "_end");
            emitLabel(base + "_else");
            visit(ctx->statement(1));
            emitLabel(base + "_end");
        } else {
            emit("JE " + base + "_end");
            visit(ctx->statement(0));
            emitLabel(base + "_end");
        }
        return {};
    }
    if (ctx->WHILE()) {
        annotateLine(line);
        std::string base = newLabelBase();
        emitLabel(base + "_start");
        visit(ctx->expression());
        emit("TEST EAX, EAX");
        emit("JE " + base + "_end");
        visit(ctx->statement(0));
        emit("JMP " + base + "_start");
        emitLabel(base + "_end");
        return {};
    }
    if (ctx->FOR()) {
        annotateLine(line);
        std::string base = newLabelBase();
        auto* initEs = ctx->expression_statement(0);
        if (initEs->expression()) visit(initEs->expression());
        emitLabel(base + "_start");
        auto* condEs = ctx->expression_statement(1);
        if (condEs->expression()) {
            visit(condEs->expression());
            emit("TEST EAX, EAX");
            emit("JE " + base + "_end");
        }
        visit(ctx->statement(0));
        if (ctx->expression()) visit(ctx->expression());
        emit("JMP " + base + "_start");
        emitLabel(base + "_end");
        return {};
    }

    if (ctx->GOTO()) {
        annotateLine(line);
        std::string label = ctx->ID()->getText();
        emit("JMP label_" + label);
        return {};
    }
    if (ctx->COLON()) {
        emitLabel("label_" + ctx->ID()->getText());
        return {};
    }
    return {};
}

// ==================== expression ====================
std::any CodeGenVisitor::visitExpression(CSubsetParser::ExpressionContext* ctx) {
    if (ctx->ASSIGNOP()) {
        visit(ctx->expression());   // RHS -> EAX
        emit("PUSH EAX");
        std::string operand = addressOf(ctx->variable());
        emit("POP EAX");
        if (!operand.empty()) emit("MOV " + operand + ", EAX");
        return {};
    }
    if (ctx->COMPOUND_ASSIGNOP()) {
        std::string operand1 = addressOf(ctx->variable());
        emit("MOV ECX, " + (operand1.empty() ? std::string("0") : operand1)); // ECX = old value

        visit(ctx->logic_expression());   // RHS -> EAX
        emit("MOV EBX, EAX");             // EBX = rhs
        emit("MOV EAX, ECX");             // EAX = old (left)

        std::string op = ctx->COMPOUND_ASSIGNOP()->getText();
        if (op == "+=") {
            emit("ADD EAX, EBX");
        } else if (op == "-=") {
            emit("SUB EAX, EBX");
        } else if (op == "*=") {
            emit("MUL EBX");
        } else if (op == "/=") {
            emit("CDQ");
            emit("IDIV EBX");
        } else {
            emit("CDQ");
            emit("IDIV EBX");
            emit("MOV EAX, EDX");
        }

        std::string operand2 = addressOf(ctx->variable()); // recompute address for the store
        if (!operand2.empty()) emit("MOV " + operand2 + ", EAX");
        return {};
    }
    visit(ctx->logic_expression());
    return {};
}

// ==================== logic_expression ====================
std::any CodeGenVisitor::visitLogic_expression(CSubsetParser::Logic_expressionContext* ctx) {
    if (ctx->LOGICOP()) {
        std::string op = ctx->LOGICOP()->getText();
        std::string base = newLabelBase();
        visit(ctx->rel_expression(0)); // left -> EAX
        emit("TEST EAX, EAX"); // to check eax
        if (op == "&&") {
            // left false -> short-circuit, result 0; else result = right's value
            emit("JE " + base + "_short");
            visit(ctx->rel_expression(1));
            emit("JMP " + base + "_end");
            emitLabel(base + "_short");
            emit("MOV EAX, 0");
            emitLabel(base + "_end");
        } else { // "||"
            // left true -> short-circuit, result 1; else result = right's value
            emit("JNE " + base + "_short");
            visit(ctx->rel_expression(1));
            emit("JMP " + base + "_end");
            emitLabel(base + "_short");
            emit("MOV EAX, 1");
            emitLabel(base + "_end");
        }
        return {};
    }
    visit(ctx->rel_expression(0));
    return {};
}

// ==================== rel_expression ====================
void CodeGenVisitor::genCompare(CSubsetParser::Simple_expressionContext* left,
                                 CSubsetParser::Simple_expressionContext* right,
                                 const std::string& op) {
    visit(right);
    emit("PUSH EAX");
    visit(left);
    emit("POP EBX");
    emit("CMP EAX, EBX");

    std::string base = newLabelBase();
    std::string trueLabel = base + "_true";
    std::string endLabel = base + "_end";
    std::string jmp = (op == "<") ? "JL" : (op == ">") ? "JG" : (op == "<=") ? "JLE"
                     : (op == ">=") ? "JGE" : (op == "==") ? "JE" : "JNE";
    emit(jmp + " " + trueLabel);
    emit("MOV EAX, 0");
    emit("JMP " + endLabel);
    emitLabel(trueLabel);
    emit("MOV EAX, 1");
    emitLabel(endLabel);
}

std::any CodeGenVisitor::visitRel_expression(CSubsetParser::Rel_expressionContext* ctx) {
    if (ctx->RELOP()) {
        genCompare(ctx->simple_expression(0), ctx->simple_expression(1), ctx->RELOP()->getText());
        return {};
    }
    visit(ctx->simple_expression(0));
    return {};
}

// ==================== simple_expression ====================
std::any CodeGenVisitor::visitSimple_expression(CSubsetParser::Simple_expressionContext* ctx) {
    if (ctx->ADDOP()) {
        visit(ctx->term());
        emit("PUSH EAX");
        visit(ctx->simple_expression());
        emit("POP EBX");
        emit(ctx->ADDOP()->getText() == "+" ? "ADD EAX, EBX" : "SUB EAX, EBX");
        return {};
    }
    visit(ctx->term());
    return {};
}

// ==================== term ====================
std::any CodeGenVisitor::visitTerm(CSubsetParser::TermContext* ctx) {
    if (ctx->MULOP()) {
        visit(ctx->unary_expression());
        emit("PUSH EAX");
        visit(ctx->term());
        emit("POP EBX");
        std::string op = ctx->MULOP()->getText();
        if (op == "*") {
            emit("MUL EBX");
        } else if (op == "/") {
            emit("CDQ");
            emit("IDIV EBX");
        } else { // "%"
            emit("CDQ");
            emit("IDIV EBX");
            emit("MOV EAX, EDX");
        }
        return {};
    }
    visit(ctx->unary_expression());
    return {};
}

// ==================== unary_expression ====================
std::any CodeGenVisitor::visitUnary_expression(CSubsetParser::Unary_expressionContext* ctx) {
    if (ctx->NOT()) {
        visit(ctx->unary_expression());
        std::string base = newLabelBase();
        emit("TEST EAX, EAX");
        emit("JNE " + base + "_true");
        emit("MOV EAX, 1");
        emit("JMP " + base + "_end");
        emitLabel(base + "_true");
        emit("MOV EAX, 0");
        emitLabel(base + "_end");
        return {};
    }
    if (ctx->ADDOP()) {
        visit(ctx->unary_expression());
        if (ctx->ADDOP()->getText() == "-") emit("NEG EAX");
        return {};
    }
    visit(ctx->factor());
    return {};
}

// ==================== argument_list / arguments ====================
void CodeGenVisitor::collectArguments(CSubsetParser::ArgumentsContext* ctx,
                                       std::vector<CSubsetParser::Logic_expressionContext*>& out) {
    if (ctx->arguments()) collectArguments(ctx->arguments(), out);
    out.push_back(ctx->logic_expression());
}

// ==================== factor ====================
std::any CodeGenVisitor::visitFactor(CSubsetParser::FactorContext* ctx) {
    if (ctx->CONST_INT()) {
        emit("MOV EAX, " + ctx->CONST_INT()->getText());
        return {};
    }
    if (ctx->CONST_FLOAT()) {
        emitComment("[Phase 1] floating point not required - truncated to int");
        int v = (int) std::stod(ctx->CONST_FLOAT()->getText());
        emit("MOV EAX, " + std::to_string(v));
        return {};
    }
    if (ctx->LPAREN() && ctx->expression()) {
        visit(ctx->expression());
        return {};
    }
    // function calling
    if (ctx->ID() && ctx->LPAREN()) {
        std::string funcName = ctx->ID()->getText();
        std::string label = (funcName == "main") ? "main" : ("func_" + funcName);

        std::vector<CSubsetParser::Logic_expressionContext*> args;
        if (ctx->argument_list()->arguments()) collectArguments(ctx->argument_list()->arguments(), args);

        // push right-to-left, callee cleans up (RET N)
        for (auto it = args.rbegin(); it != args.rend(); ++it) {
            visit(*it);
            emit("PUSH EAX");
        }
        emit("CALL " + label);
        return {};
    }
    if (ctx->INCOP() || ctx->DECOP()) {
        std::string operand = addressOf(ctx->variable());
        if (!operand.empty()) {
            emit(std::string("PUSH dword ") + operand);
            emit((ctx->INCOP() ? std::string("INC dword ") : std::string("DEC dword ")) + operand);
            emit("POP EAX");
        } else {
            emit("MOV EAX, 0");
        }
        return {};
    }
    // factor : variable
    std::string operand = addressOf(ctx->variable());
    emit("MOV EAX, " + (operand.empty() ? "0" : operand));
    return {};
}

void CodeGenVisitor::printArguments(CSubsetParser::ArgumentsContext* ctx) {
    if (ctx->arguments()) printArguments(ctx->arguments());  // earlier args first
    visit(ctx->logic_expression());                          // -> EAX
    emit("CALL OUTDEC");
}
