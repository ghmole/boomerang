#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "FromSSAFormPass.h"

#include "boomerang/core/Project.h"
#include "boomerang/core/Settings.h"
#include "boomerang/db/Prog.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/decomp/InterferenceFinder.h"
#include "boomerang/passes/PassManager.h"
#include "boomerang/ssl/exp/Location.h"
#include "boomerang/ssl/exp/RefExp.h"
#include "boomerang/ssl/statements/PhiAssign.h"
#include "boomerang/ssl/type/VoidType.h"
#include "boomerang/util/ConnectionGraph.h"
#include "boomerang/util/log/Log.h"
#include "boomerang/visitor/expmodifier/ExpSSAXformer.h"


FromSSAFormPass::FromSSAFormPass()
    : IPass("FromSSAForm", PassID::FromSSAForm)
{}


bool FromSSAFormPass::execute(UserProc *proc)
{
    proc->getProg()->getProject()->alertDecompiling(proc);

    StatementList stmts;
    proc->getStatements(stmts);

    for (Statement *s : stmts) {
        // Map registers to initial local variables
        s->mapRegistersToLocals();
        // Insert casts where needed, as types are about to become inaccessible
        s->insertCasts();
    }

    // First split the live ranges where needed by reason of type incompatibility, i.e. when the
    // type of a subscripted variable is different to its previous type. Start at the top, because
    // we don't want to rename parameters (e.g. argc)
    typedef std::pair<SharedType, SharedExp> FirstTypeEnt;
    typedef std::map<SharedExp, FirstTypeEnt, lessExpStar> FirstTypesMap;

    FirstTypesMap firstTypes;
    FirstTypesMap::iterator ff;
    ConnectionGraph ig; // The interference graph; these can't have the same local variable
    ConnectionGraph pu; // The Phi Unites: these need the same local variable or copies
    const bool assumeABICompliance = proc->getProg()->getProject()->getSettings()->assumeABI;

    for (Statement *s : stmts) {
        LocationSet defs;
        s->getDefinitions(defs, assumeABICompliance);

        for (SharedExp base : defs) {
            SharedType ty = s->getTypeFor(base);

            if (ty == nullptr) { // Can happen e.g. when getting the type for %flags
                ty = VoidType::get();
            }

            LOG_VERBOSE2("Got type %1 for %2 from %3", ty->prints(), base, s);
            ff            = firstTypes.find(base);
            SharedExp ref = RefExp::get(base, s);

            if (ff == firstTypes.end()) {
                // There is no first type yet. Record it.
                FirstTypeEnt fte;
                fte.first        = ty;
                fte.second       = ref;
                firstTypes[base] = fte;
            }
            else if (ff->second.first && !ty->isCompatibleWith(*ff->second.first)) {
                if (proc->getProg()->getProject()->getSettings()->debugLiveness) {
                    LOG_MSG("Def of %1 at %2 type %3 is not compatible with first type %4.", base,
                            s->getNumber(), ty, ff->second.first);
                }

                // There already is a type for base, and it is different to the type for this
                // definition. Record an "interference" so it will get a new variable
                if (!ty->isVoid()) { // just ignore void interferences ??!!
                    ig.connect(ref, ff->second.second);
                }
            }
        }
    }
    assert(ig.allRefsHaveDefs());

    // Find the interferences generated by more than one version of a variable being live at the
    // same program point
    InterferenceFinder(proc->getCFG()).findInterferences(ig);
    assert(ig.allRefsHaveDefs());

    // Find the set of locations that are "united" by phi-functions
    // FIXME: are these going to be trivially predictable?
    findPhiUnites(proc, pu);

    if (proc->getProg()->getProject()->getSettings()->debugLiveness) {
        LOG_MSG("## ig interference graph:");

        for (ConnectionGraph::iterator ii = ig.begin(); ii != ig.end(); ++ii) {
            LOG_MSG("   ig %1 -> %2", ii->first, ii->second);
        }

        LOG_MSG("## pu phi unites graph:");

        for (ConnectionGraph::iterator ii = pu.begin(); ii != pu.end(); ++ii) {
            LOG_MSG("   pu %1 -> %2", ii->first, ii->second);
        }
    }

    // Choose one of each interfering location to give a new name to
    assert(ig.allRefsHaveDefs());

    for (ConnectionGraph::iterator ii = ig.begin(); ii != ig.end(); ++ii) {
        auto r1       = ii->first->access<RefExp>();
        auto r2       = ii->second->access<RefExp>(); // r1 -> r2 and vice versa
        QString name1 = proc->lookupSymFromRefAny(r1);
        QString name2 = proc->lookupSymFromRefAny(r2);

        if (!name1.isEmpty() && !name2.isEmpty() && (name1 != name2)) {
            continue; // Already different names, probably because of the redundant mapping
        }

        std::shared_ptr<RefExp> rename;

        if (r1->isImplicitDef()) {
            // If r1 is an implicit definition, don't rename it (it is probably a parameter, and
            // should retain its current name)
            rename = r2;
        }
        else if (r2->isImplicitDef()) {
            rename = r1; // Ditto for r2
        }

        if (rename == nullptr) {
            Statement *def2 = r2->getDef();

            if (def2->isPhi()) { // Prefer the destinations of phis
                rename = r2;
            }
            else {
                rename = r1;
            }
        }

        SharedType ty   = rename->getDef()->getTypeFor(rename->getSubExp1());
        SharedExp local = proc->createLocal(ty, rename);

        if (proc->getProg()->getProject()->getSettings()->debugLiveness) {
            LOG_MSG("Renaming %1 to %2", rename, local);
        }

        proc->mapSymbolTo(rename, local);
    }

    // Implement part of the Phi Unites list, where renamings or parameters have broken them, by
    // renaming The rest of them will be done as phis are removed The idea is that where l1 and l2
    // have to unite, and exactly one of them already has a local/name, you can implement the
    // unification by giving the unnamed one the same name as the named one, as long as they don't
    // interfere
    for (ConnectionGraph::iterator ii = pu.begin(); ii != pu.end(); ++ii) {
        auto r1       = ii->first->access<RefExp>();
        auto r2       = ii->second->access<RefExp>();
        QString name1 = proc->lookupSymFromRef(r1);
        QString name2 = proc->lookupSymFromRef(r2);

        if (!name1.isEmpty() && !name2.isEmpty() && !ig.isConnected(r1, *r2)) {
            // There is a case where this is unhelpful, and it happen in test/pentium/fromssa2. We
            // have renamed the destination of the phi to ebx_1, and that leaves the two phi
            // operands as ebx. However, we attempt to unite them here, which will cause one of the
            // operands to become ebx_1, so the neat oprimisation of replacing the phi with one copy
            // doesn't work. The result is an extra copy. So check of r1 is a phi and r2 one of its
            // operands, and all other operands for the phi have the same name. If so, don't rename.
            Statement *def1 = r1->getDef();

            if (def1->isPhi()) {
                bool allSame     = true;
                bool r2IsOperand = false;
                QString firstName;
                PhiAssign *pa = static_cast<PhiAssign *>(def1);

                for (RefExp &refExp : *pa) {
                    auto re(RefExp::get(refExp.getSubExp1(), refExp.getDef()));

                    if (*re == *r2) {
                        r2IsOperand = true;
                    }

                    if (firstName.isEmpty()) {
                        firstName = proc->lookupSymFromRefAny(re);
                    }
                    else {
                        QString tmp = proc->lookupSymFromRefAny(re);

                        if (tmp.isEmpty() || (firstName != tmp)) {
                            allSame = false;
                            break;
                        }
                    }
                }

                if (allSame && r2IsOperand) {
                    continue; // This situation has happened, don't map now
                }
            }

            proc->mapSymbolTo(r2, Location::local(name1, proc));
            continue;
        }
    }

    /*    *    *    *    *    *    *    *    *    *    *    *    *    *    *\
    *                                                        *
    *     IR gets changed with hard locals and params here    *
    *                                                        *
    \*    *    *    *    *    *    *    *    *    *    *    *    *    *    */

    // First rename the variables (including phi's, but don't remove).
    // NOTE: it is not possible to postpone renaming these locals till the back end, since the same
    // base location may require different names at different locations, e.g. r28{0} is local0,
    // r28{16} is local1 Update symbols and parameters, particularly for the stack pointer inside
    // memofs. NOTE: the ordering of the below operations is critical! Re-ordering may well prevent
    // e.g. parameters from renaming successfully.
    assert(proc->allPhisHaveDefs());
    nameParameterPhis(proc);
    PassManager::get()->executePass(PassID::LocalAndParamMap, proc);
    mapParameters(proc);
    removeSubscriptsFromSymbols(proc);
    removeSubscriptsFromParameters(proc);

    for (Statement *s : stmts) {
        s->replaceSubscriptsWithLocals();
    }

    // Now remove the phis
    for (Statement *s : stmts) {
        if (!s->isPhi()) {
            continue;
        }

        // Check if the base variables are all the same
        PhiAssign *phi = static_cast<PhiAssign *>(s);

        if (phi->begin() == phi->end()) {
            // no params to this phi, just remove it
            LOG_VERBOSE("Phi with no params, removing: %1", s);

            proc->removeStatement(s);
            continue;
        }

        LocationSet refs;
        phi->addUsedLocs(refs);
        bool phiParamsSame = true;
        SharedExp first    = nullptr;

        if (phi->getNumDefs() > 1) {
            for (RefExp &pi : *phi) {
                if (pi.getSubExp1() == nullptr) {
                    continue;
                }

                if (first == nullptr) {
                    first = pi.getSubExp1();
                    continue;
                }

                if (!(*(pi.getSubExp1()) == *first)) {
                    phiParamsSame = false;
                    break;
                }
            }
        }

        if (phiParamsSame && first) {
            // Is the left of the phi assignment the same base variable as all the operands?
            if (*phi->getLeft() == *first) {
                if (proc->getProg()->getProject()->getSettings()->debugLiveness ||
                    proc->getProg()->getProject()->getSettings()->debugUnused) {
                    LOG_MSG("Removing phi: left and all refs same or 0: %1", s);
                }

                // Just removing the refs will work, or removing the whole phi
                // NOTE: Removing the phi here may cause other statments to be not used.
                proc->removeStatement(s);
            }
            else {
                // Need to replace the phi by an expression,
                // e.g. local0 = phi(r24{3}, r24{5}) becomes
                //        local0 = r24
                phi->convertToAssign(first->clone());
            }
        }
        else {
            // Need new local(s) for phi operands that have different names from the lhs

            // This way is costly in copies, but has no problems with extending live ranges
            // Exp* tempLoc = newLocal(pa->getType());
            SharedExp tempLoc = proc->getSymbolExp(RefExp::get(phi->getLeft(), phi),
                                                   phi->getType());

            if (proc->getProg()->getProject()->getSettings()->debugLiveness) {
                LOG_MSG("Phi statement %1 requires local, using %2", s, tempLoc);
            }

            // For each definition ref'd in the phi
            for (RefExp &pi : *phi) {
                if (pi.getSubExp1() == nullptr) {
                    continue;
                }

                proc->insertAssignAfter(pi.getDef(), tempLoc, pi.getSubExp1());
            }

            // Replace the RHS of the phi with tempLoc
            phi->convertToAssign(tempLoc);
        }
    }

    return true;
}


void FromSSAFormPass::nameParameterPhis(UserProc *proc)
{
    StatementList stmts;
    proc->getStatements(stmts);

    for (Statement *insn : stmts) {
        if (!insn->isPhi()) {
            continue; // Might be able to optimise this a bit
        }

        PhiAssign *pi = static_cast<PhiAssign *>(insn);
        // See if the destination has a symbol already
        SharedExp lhs = pi->getLeft();
        auto lhsRef   = RefExp::get(lhs, pi);

        if (proc->findFirstSymbol(lhsRef) != nullptr) {
            continue; // Already mapped to something
        }

        bool multiple = false; // True if find more than one unique parameter
        QString firstName;     // The name for the first parameter found
        SharedType ty = pi->getType();

        for (RefExp &v : *pi) {
            if (v.getDef()->isImplicit()) {
                QString name = proc->lookupSym(RefExp::get(v.getSubExp1(), v.getDef()), ty);

                if (!name.isEmpty()) {
                    if (!firstName.isEmpty() && (firstName != name)) {
                        multiple = true;
                        break;
                    }

                    firstName = name; // Remember this candidate
                }
            }
        }

        if (multiple || firstName.isEmpty()) {
            continue;
        }

        proc->mapSymbolTo(lhsRef, Location::param(firstName, proc));
    }
}

void FromSSAFormPass::mapParameters(UserProc *proc)
{
    // Replace the parameters with their mappings
    StatementList::iterator pp;

    for (pp = proc->getParameters().begin(); pp != proc->getParameters().end(); ++pp) {
        SharedExp lhs      = static_cast<Assignment *>(*pp)->getLeft();
        QString mappedName = proc->lookupParam(lhs);

        if (mappedName.isEmpty()) {
            LOG_WARN("No symbol mapping for parameter %1", lhs);
            bool allZero;
            SharedExp clean = lhs->clone()->removeSubscripts(allZero);

            if (allZero) {
                static_cast<Assignment *>(*pp)->setLeft(clean);
            }

            // Else leave them alone
        }
        else {
            static_cast<Assignment *>(*pp)->setLeft(Location::param(mappedName, proc));
        }
    }
}


void FromSSAFormPass::removeSubscriptsFromSymbols(UserProc *proc)
{
    // Basically, use the symbol map to map the symbols in the symbol map!
    // However, do not remove subscripts from the outer level; they are still needed for comments in
    // the output and also for when removing subscripts from parameters (still need the {0}) Since
    // this will potentially change the ordering of entries, need to copy the map
    UserProc::SymbolMap sm2 = proc->getSymbolMap(); // Object copy

    proc->getSymbolMap().clear();
    ExpSSAXformer esx(proc);

    for (auto it = sm2.begin(); it != sm2.end(); ++it) {
        SharedExp from = std::const_pointer_cast<Exp>(it->first);

        if (from->isSubscript()) {
            // As noted above, don't touch the outer level of subscripts
            SharedExp &sub = from->refSubExp1();
            sub            = sub->acceptModifier(&esx);
        }
        else {
            from = from->acceptModifier(&esx);
        }

        proc->mapSymbolTo(from, it->second);
    }
}


void FromSSAFormPass::removeSubscriptsFromParameters(UserProc *proc)
{
    ExpSSAXformer esx(proc);

    for (Statement *param : proc->getParameters()) {
        SharedExp left = static_cast<Assignment *>(param)->getLeft();
        left           = left->acceptModifier(&esx);
        static_cast<Assignment *>(param)->setLeft(left);
    }
}


void FromSSAFormPass::findPhiUnites(UserProc *proc, ConnectionGraph &pu)
{
    StatementList stmts;
    proc->getStatements(stmts);

    for (Statement *stmt : stmts) {
        if (!stmt->isPhi()) {
            continue;
        }

        PhiAssign *pa = static_cast<PhiAssign *>(stmt);
        SharedExp lhs = pa->getLeft();
        auto reLhs    = RefExp::get(lhs, pa);

        for (RefExp &v : *pa) {
            assert(v.getSubExp1());
            auto re = RefExp::get(v.getSubExp1(), v.getDef());
            pu.connect(reLhs, re);
        }
    }
}
