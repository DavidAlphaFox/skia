/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/sksl/dsl/priv/DSLWriter.h"

#include "include/core/SkTypes.h"
#include "include/private/SkSLDefines.h"
#include "src/sksl/SkSLContext.h"
#include "src/sksl/SkSLModifiersPool.h"
#include "src/sksl/SkSLPosition.h"
#include "src/sksl/SkSLThreadContext.h"
#include "src/sksl/dsl/DSLExpression.h"
#include "src/sksl/dsl/DSLStatement.h"
#include "src/sksl/dsl/DSLType.h"
#include "src/sksl/dsl/DSLVar.h"
#include "src/sksl/ir/SkSLBlock.h"
#include "src/sksl/ir/SkSLNop.h"
#include "src/sksl/ir/SkSLProgramElement.h"
#include "src/sksl/ir/SkSLStatement.h"
#include "src/sksl/ir/SkSLSymbolTable.h"
#include "src/sksl/ir/SkSLVarDeclarations.h"
#include "src/sksl/ir/SkSLVariable.h"

#include <utility>
#include <vector>

namespace SkSL {

namespace dsl {

SkSL::Variable* DSLWriter::Var(DSLVarBase& var) {
    // fInitialized is true if we have attempted to create a var, whether or not we actually
    // succeeded. If it's true, we don't want to try again, to avoid reporting the same error
    // multiple times.
    if (!var.fInitialized) {
        // We haven't even attempted to create a var yet, so fVar and fDeclaration ought to be null
        SkASSERT(!var.fVar);
        SkASSERT(!var.fDeclaration);

        var.fInitialized = true;
        std::unique_ptr<SkSL::Variable> skslvar = SkSL::Variable::Convert(ThreadContext::Context(),
                                                                          var.fPosition,
                                                                          var.fModifiersPos,
                                                                          var.fModifiers,
                                                                          &var.fType.skslType(),
                                                                          var.fNamePosition,
                                                                          var.fName,
                                                                          var.fStorage);
        if (var.fStorage != SkSL::VariableStorage::kParameter) {
            var.fDeclaration = VarDeclaration::Convert(ThreadContext::Context(),
                                                       std::move(skslvar),
                                                       var.fInitialValue.releaseIfPossible());
            if (var.fDeclaration) {
                var.fVar = var.fDeclaration->as<VarDeclaration>().var();
            }
        }
    }
    return var.fVar;
}

std::unique_ptr<SkSL::Variable> DSLWriter::CreateParameterVar(DSLParameter& var) {
    // This should only be called on undeclared parameter variables, but we allow the creation to go
    // ahead regardless so we don't have to worry about null pointers potentially sneaking in and
    // breaking things. DSLFunction is responsible for reporting errors for invalid parameters.
    return SkSL::Variable::Convert(ThreadContext::Context(),
                                   var.fPosition,
                                   var.fModifiersPos,
                                   var.fModifiers,
                                   &var.fType.skslType(),
                                   var.fNamePosition,
                                   var.fName,
                                   var.fStorage);
}

std::unique_ptr<SkSL::Statement> DSLWriter::Declaration(DSLVarBase& var) {
    Var(var);
    if (!var.fDeclaration) {
        // We should have already reported an error before ending up here, just clean up the
        // initial value so it doesn't assert and return a nop.
        var.fInitialValue.releaseIfPossible();
        return SkSL::Nop::Make();
    }
    return std::move(var.fDeclaration);
}

void DSLWriter::AddVarDeclaration(DSLStatement& existing, DSLVar& additional) {
    std::unique_ptr<Statement> stmt = existing.releaseIfPossible();
    if (!stmt || stmt->isEmpty()) {
        // If the variable declaration generated an error, we can end up with a Nop statement here.
        // Jettison the existing statement, and keep the new declaration.
        existing = DSLStatement(Declaration(additional));
        return;
    }

    if (stmt->is<Block>()) {
        SkSL::Block& block = stmt->as<Block>();
        if (!block.isScope()) {
            // The existing statement is a Block without curly braces; append the additional
            // declaration to the end.
            block.children().push_back(Declaration(additional));
            existing = DSLStatement(std::move(stmt));
            return;
        }
    }

    // The statement was not a Block; create a compound-statement block to hold the existing
    // statement alongside the additional one.
    Position pos = stmt->fPosition;
    StatementArray stmts;
    stmts.reserve_back(2);
    stmts.push_back(std::move(stmt));
    stmts.push_back(Declaration(additional));
    existing = DSLStatement(
            SkSL::Block::Make(pos, std::move(stmts), Block::Kind::kCompoundStatement),
            pos);
}

void DSLWriter::Reset() {
    SkSL::Context& context = ThreadContext::Context();

    SymbolTable::Pop(&context.fSymbolTable);
    SymbolTable::Push(&context.fSymbolTable);
    ThreadContext::ProgramElements().clear();
    context.fModifiersPool->clear();
}

} // namespace dsl

} // namespace SkSL
