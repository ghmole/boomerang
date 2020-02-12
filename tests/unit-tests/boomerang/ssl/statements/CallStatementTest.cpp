#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "CallStatementTest.h"


#include "boomerang/core/Settings.h"
#include "boomerang/db/LowLevelCFG.h"
#include "boomerang/db/Prog.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/db/proc/LibProc.h"
#include "boomerang/db/proc/ProcCFG.h"
#include "boomerang/db/signature/X86Signature.h"
#include "boomerang/passes/PassManager.h"
#include "boomerang/ssl/exp/Binary.h"
#include "boomerang/ssl/exp/Const.h"
#include "boomerang/ssl/exp/Location.h"
#include "boomerang/ssl/exp/RefExp.h"
#include "boomerang/ssl/statements/CallStatement.h"
#include "boomerang/ssl/statements/ImplicitAssign.h"
#include "boomerang/ssl/statements/ReturnStatement.h"
#include "boomerang/ssl/type/CharType.h"
#include "boomerang/ssl/type/IntegerType.h"
#include "boomerang/ssl/type/PointerType.h"
#include "boomerang/util/LocationSet.h"


void CallStatementTest::testClone()
{
    SharedExp ecx = Location::regOf(REG_X86_ECX);

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        SharedStmt clone = call->clone();

        QVERIFY(&(*clone) != &(*call));
        QVERIFY(clone->isCall());
        QVERIFY(clone->getID() != (uint32)-1);
        QVERIFY(clone->getID() != call->getID());

        std::shared_ptr<CallStatement> callClone = clone->as<CallStatement>();

        QVERIFY(callClone->getDest() != nullptr);
        QCOMPARE(*callClone->getDest(), *call->getDest());
        QVERIFY(!callClone->isComputed());

        QCOMPARE(callClone->getDestProc(), nullptr);
        QCOMPARE(callClone->isReturnAfterCall(), false);
        QCOMPARE(callClone->getSignature(), nullptr);
    }

    {
        std::shared_ptr<CallStatement> call(new CallStatement(ecx));
        SharedStmt clone = call->clone();

        QVERIFY(&(*clone) != &(*call));
        QVERIFY(clone->isCall());
        QVERIFY(clone->getID() != (uint32)-1);
        QVERIFY(clone->getID() != call->getID());

        std::shared_ptr<CallStatement> callClone = clone->as<CallStatement>();

        QVERIFY(callClone->getDest() != nullptr);
        QCOMPARE(*callClone->getDest(), *call->getDest());
        QVERIFY(callClone->isComputed());

        QCOMPARE(callClone->getDestProc(), nullptr);
        QCOMPARE(callClone->isReturnAfterCall(), false);
        QCOMPARE(callClone->getSignature(), nullptr);
    }
}


void CallStatementTest::testNumber()
{
    const SharedExp ebx = Location::regOf(REG_X86_EBX);

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        QCOMPARE(call->getNumber(), 0);

        call->setNumber(1);
        QCOMPARE(call->getNumber(), 1);
    }

    {
        StatementList args;
        args.append(std::make_shared<Assign>(ebx, ebx));

        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->setArguments(args);

        for (SharedStmt arg : call->getArguments()) {
            QCOMPARE(arg->getNumber(), call->getNumber());
        }

        call->setNumber(42);
        QCOMPARE(call->getNumber(), 42);

        for (SharedStmt arg : call->getArguments()) {
            QCOMPARE(arg->getNumber(), call->getNumber());
        }
    }
}


void CallStatementTest::testGetDefinitions()
{
    {
        LocationSet defs;
        std::shared_ptr<CallStatement> call(new CallStatement(Location::regOf(REG_X86_ECX)));
        QVERIFY(call->isChildless());
        QVERIFY(call->getDefines().empty());

        call->getDefinitions(defs, true);
        QCOMPARE(defs.toString(), "");

        defs.clear();
        call->getDefinitions(defs, false);
        QCOMPARE(defs.toString(), "<all>");
    }

    {
        LocationSet defs;
        StatementList callDefines;

        std::shared_ptr<CallStatement> call(new CallStatement(Location::regOf(REG_X86_ECX)));
        callDefines.append(std::make_shared<ImplicitAssign>(Location::regOf(REG_X86_EAX)));
        call->setDefines(callDefines);

        call->getDefinitions(defs, true);
        QCOMPARE(defs.toString(), "r24");
    }
}


void CallStatementTest::testDefinesLoc()
{
    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->getDefines().append(std::make_shared<Assign>(Location::regOf(REG_X86_EAX), Location::regOf(REG_X86_ECX)));

        QVERIFY(!call->definesLoc(Location::regOf(REG_X86_ECX)));
        QVERIFY( call->definesLoc(Location::regOf(REG_X86_EAX)));
        QVERIFY(!call->definesLoc(Const::get(0x1000)));
    }
}


void CallStatementTest::testSearch()
{
    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));

        SharedExp result;
        QVERIFY(!call->search(*Const::get(0), result));
    }

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        SharedExp result;
        QVERIFY(call->search(*Const::get(0x1000), result));
        QVERIFY(result != nullptr);
        QCOMPARE(*result, *Const::get(0x1000));
    }

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->getDefines().append(std::make_shared<Assign>(Location::regOf(REG_X86_ECX), Const::get(0)));

        SharedExp result;
        QVERIFY(call->search(*Location::regOf(REG_X86_ECX), result));
        QVERIFY(result != nullptr);
        QCOMPARE(*result, *Location::regOf(REG_X86_ECX));
    }

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->getArguments().append(std::make_shared<Assign>(Location::regOf(REG_X86_ECX), Const::get(0)));

        SharedExp result;
        QVERIFY(call->search(*Location::regOf(REG_X86_ECX), result));
        QVERIFY(result != nullptr);
        QCOMPARE(*result, *Location::regOf(REG_X86_ECX));
    }
}


void CallStatementTest::testSearchAll()
{
    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));

        std::list<SharedExp> result;
        QVERIFY(!call->searchAll(*Const::get(0), result));
        QVERIFY(result.empty());
    }

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));

        std::list<SharedExp> result;
        QVERIFY(call->searchAll(*Const::get(0x1000), result));
        QCOMPARE(result, { call->getDest() });
    }

    {
        SharedExp ecx = Location::regOf(REG_X86_ECX);
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->getDefines().append(std::make_shared<Assign>(ecx, Const::get(0)));

        std::list<SharedExp> result;
        QVERIFY(call->searchAll(*Location::regOf(REG_X86_ECX), result));
        QCOMPARE(result, { ecx });
    }

    {
        SharedExp ecx = Location::regOf(REG_X86_ECX);
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->getArguments().append(std::make_shared<Assign>(ecx, Const::get(0)));

        std::list<SharedExp> result;
        QVERIFY(call->searchAll(*Location::regOf(REG_X86_ECX), result));
        QCOMPARE(result, { ecx });
    }
}


void CallStatementTest::testSearchAndReplace()
{
    QSKIP("TODO");
}


void CallStatementTest::testSimplify()
{
    const SharedExp ecx = Location::regOf(REG_X86_ECX);

    {
        std::shared_ptr<CallStatement> call(new CallStatement(
            Binary::get(opPlus, Const::get(0x800), Const::get(0x800))));
        QVERIFY(call->isComputed());

        call->simplify();

        QVERIFY(call->getDest() != nullptr);
        QCOMPARE(*call->getDest(), *Const::get(0x1000));

        // FIXME: This call should be not computed, but updating the computed flag
        // breaks regression tests
        QVERIFY(call->isComputed());
    }

    {
        StatementList args, defs;
        args.append(std::make_shared<Assign>(ecx, Binary::get(opPlus, Const::get(40), Const::get(2))));
        defs.append(args.front()->clone());

        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->setArguments(args);
        call->setDefines(defs);
        call->simplify();

        QCOMPARE(call->getArguments().toString(), "   0 *v* r25 := 42");
        QCOMPARE(call->getDefines().toString(),   "   0 *v* r25 := 42");
    }
}


void CallStatementTest::testTypeForExp()
{
    const SharedExp ecx = Location::regOf(REG_X86_ECX);

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));

        SharedConstType ty = call->getTypeForExp(ecx);
        QVERIFY(ty != nullptr);
        QCOMPARE(*ty, *VoidType::get());
    }

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));

        SharedConstType ty = call->getTypeForExp(Terminal::get(opPC));
        QVERIFY(ty != nullptr);
        QCOMPARE(*ty, *PointerType::get(VoidType::get()));
    }

    {
        StatementList defs;
        defs.append(std::make_shared<Assign>(IntegerType::get(32, Sign::Signed), ecx, Const::get(0)));

        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->setDefines(defs);

        SharedConstType ty = call->getTypeForExp(Const::get(0));
        QVERIFY(ty != nullptr);
        QCOMPARE(*ty, *VoidType::get());

        ty = call->getTypeForExp(ecx->clone()); // verify it is not comparing by address
        QVERIFY(ty != nullptr);
        QCOMPARE(*ty, *IntegerType::get(32, Sign::Signed));
    }

    QSKIP("TODO: setTypeForExp");
}


void CallStatementTest::testToString()
{
    Prog prog("test", &m_project);
    UserProc *srcProc = static_cast<UserProc *>(prog.getOrCreateFunction(Address(0x1000)));
    LibProc *destProc = prog.getOrCreateLibraryProc("destProc");
    destProc->setSignature(Signature::instantiate(Machine::X86, CallConv::C, "destProc"));

    std::shared_ptr<CallStatement> call(new CallStatement(Address(0x2000)));
    call->setProc(srcProc);

    QCOMPARE(call->toString(),
             "   0 <all> := CALL 0x00002000(<all>)\n"
             "              Reaching definitions: <None>\n"
             "              Live variables: <None>");

    call->setDest(Location::regOf(REG_X86_ECX));

    QCOMPARE(call->toString(),
             "   0 <all> := CALL r25(<all>)\n"
             "              Reaching definitions: <None>\n"
             "              Live variables: <None>");

    call->setDestProc(destProc);

    QCOMPARE(call->toString(),
             "   0 CALL destProc(\n"
             "              )\n"
             "              Reaching definitions: <None>\n"
             "              Live variables: <None>");

    call->addDefine(std::make_shared<ImplicitAssign>(Location::regOf(REG_X86_EAX)));

    QCOMPARE(call->toString(),
             "   0 { *v* r24 } := CALL destProc(\n"
             "              )\n"
             "              Reaching definitions: <None>\n"
             "              Live variables: <None>");

    call->addDefine(std::make_shared<ImplicitAssign>(Location::regOf(REG_X86_ECX)));

    QCOMPARE(call->toString(),
             "   0 { *v* r24, *v* r25 } := CALL destProc(\n"
             "              )\n"
             "              Reaching definitions: <None>\n"
             "              Live variables: <None>");

    StatementList args;
    args.append(std::make_shared<Assign>(Location::regOf(REG_X86_ECX), Location::regOf(REG_X86_ECX)));

    call->setArguments(StatementList({ args }));

    QCOMPARE(call->toString(),
             "   0 { *v* r24, *v* r25 } := CALL destProc(\n"
             "                *v* r25 := r25\n"
             "              )\n"
             "              Reaching definitions: <None>\n"
             "              Live variables: <None>");
}


void CallStatementTest::testArguments()
{
    Prog prog("test", &m_project);
    UserProc *srcProc = static_cast<UserProc *>(prog.getOrCreateFunction(Address(0x1000)));

    const SharedExp ecx = Location::regOf(REG_X86_ECX);

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x2000)));
        QVERIFY(call->getArguments().empty());
    }

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x2000)));
        call->setArguments(StatementList());
        QVERIFY(call->getArguments().empty());
    }

    {
        StatementList args;
        args.append(std::make_shared<Assign>(ecx, ecx));
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x2000)));

        call->setArguments(args);

        QVERIFY(!call->getArguments().empty());
        QCOMPARE(call->getArguments().toString(), "   0 *v* r25 := r25");

        QCOMPARE(call->getArguments().front()->getProc(), call->getProc());
        QCOMPARE(call->getArguments().front()->getNumber(), call->getNumber());
        QCOMPARE(call->getArguments().front()->getFragment(), call->getFragment());
    }

    {
        BasicBlock *bb = prog.getCFG()->createBB(BBType::Fall, createInsns(Address(0x1000), 1));
        IRFragment *frag = srcProc->getCFG()->createFragment(FragType::Fall, createRTLs(Address(0x1000), 1, 1), bb);

        StatementList args;
        args.append(std::make_shared<Assign>(ecx, ecx));
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x2000)));
        call->setFragment(frag);
        call->setProc(srcProc);
        call->setNumber(42);

        call->setArguments(args);

        QVERIFY(!call->getArguments().empty());
        QCOMPARE(call->getArguments().toString(), "  42 *v* r25 := r25");

        QCOMPARE(call->getArguments().front()->getProc(), call->getProc());
        QCOMPARE(call->getArguments().front()->getNumber(), call->getNumber());
        QCOMPARE(call->getArguments().front()->getFragment(), call->getFragment());
    }
}


void CallStatementTest::testSetSigArguments()
{
    const SharedExp ecx = Location::regOf(REG_X86_ECX);

    Prog prog("test", &m_project);
    UserProc *srcProc = static_cast<UserProc *>(prog.getOrCreateFunction(Address(0x1000)));

    LibProc *destLibProc = prog.getOrCreateLibraryProc("desLibProc");
    destLibProc->setSignature(Signature::instantiate(Machine::X86, CallConv::C, "destLibProc"));
    destLibProc->getSignature()->addParameter("param0", ecx, IntegerType::get(32, Sign::Signed));

    UserProc *destUserProc = static_cast<UserProc *>(prog.getOrCreateFunction(Address(0x2000)));
    destUserProc->setSignature(Signature::instantiate(Machine::X86, CallConv::C, "destUserProc"));

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));

        call->setSigArguments();

        QVERIFY(call->getArguments().empty());
    }

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->setSignature(Signature::instantiate(Machine::X86, CallConv::C, "test"));
        call->setSigArguments();
        QVERIFY(call->getArguments().empty());
    }

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->setProc(srcProc);
        call->setDestProc(destUserProc);

        call->setSigArguments();

        QCOMPARE(destUserProc->getCallers().size(), 1);
        QCOMPARE(*destUserProc->getCallers().begin(), call);

        QVERIFY(call->getSignature() != nullptr);
        QCOMPARE(*call->getSignature(), *destUserProc->getSignature());
        QVERIFY(call->getArguments().empty());
    }

    {
        BasicBlock *bb = prog.getCFG()->createBB(BBType::Fall, createInsns(Address(0x1000), 1));
        IRFragment *frag = srcProc->getCFG()->createFragment(FragType::Fall, createRTLs(Address(0x1000), 1, 1), bb);

        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->setProc(srcProc);
        call->setDestProc(destLibProc);
        call->setNumber(42);
        call->setFragment(frag);

        call->setSigArguments();

        QCOMPARE(destLibProc->getCallers().size(), 1);
        QCOMPARE(*destLibProc->getCallers().begin(), call);

        QVERIFY(call->getSignature() != nullptr);
        QCOMPARE(*call->getSignature(), *destLibProc->getSignature());

        QVERIFY(call->getArguments().size() == 1);
        QCOMPARE(call->getArguments().front()->getProc(), srcProc);
        QCOMPARE(call->getArguments().front()->getFragment(), frag);
        QCOMPARE(call->getArguments().front()->toString(), "  42 *i32* r25 := r25");
    }
}


void CallStatementTest::testUpdateArguments()
{
    QSKIP("TODO");
}


void CallStatementTest::testArgumentExp()
{
    QSKIP("TODO");
}


void CallStatementTest::testNumArguments()
{
    const SharedExp ecx = Location::regOf(REG_X86_ECX);

    std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
    QCOMPARE(call->getNumArguments(), 0);

    call->getArguments().append(std::make_shared<Assign>(ecx, ecx));
    QCOMPARE(call->getNumArguments(), 1);

    QSKIP("TODO: setNumArguments");
}


void CallStatementTest::testRemoveArgument()
{
    const SharedExp ecx = Location::regOf(REG_X86_ECX);

    std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
    call->getArguments().append(std::make_shared<Assign>(ecx, ecx));
    QCOMPARE(call->getNumArguments(), 1);

    call->removeArgument(0);

    QCOMPARE(call->getNumArguments(), 0);
}


void CallStatementTest::testArgumentType()
{
    const SharedExp ecx = Location::regOf(REG_X86_ECX);

    std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
    call->getArguments().append(std::make_shared<Assign>(ecx, ecx));

    SharedType ty = call->getArgumentType(0);
    QVERIFY(ty != nullptr);
    QCOMPARE(*ty, *VoidType::get());

    call->setArgumentType(0, IntegerType::get(32, Sign::Signed));

    ty = call->getArgumentType(0);
    QVERIFY(ty != nullptr);
    QCOMPARE(*ty, *IntegerType::get(32, Sign::Signed));
}


void CallStatementTest::testEliminateDuplicateArgs()
{
    const SharedExp ebx = Location::regOf(REG_X86_EBX);
    const SharedExp ecx = Location::regOf(REG_X86_ECX);

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));

        call->eliminateDuplicateArgs();
        QCOMPARE(call->getNumArguments(), 0);
    }

    {
        StatementList args;
        args.append(std::make_shared<Assign>(ebx, ebx));

        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->setArguments(args);

        call->eliminateDuplicateArgs();

        QCOMPARE(call->getNumArguments(), 1);
        QCOMPARE(call->getArguments().toString(), "   0 *v* r27 := r27");
    }

    {
        StatementList args;
        args.append(std::make_shared<Assign>(ebx, ebx));
        args.append(std::make_shared<Assign>(ebx, ebx));

        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->setArguments(args);

        call->eliminateDuplicateArgs();

        QCOMPARE(call->getNumArguments(), 1);
        QCOMPARE(call->getArguments().toString(), "   0 *v* r27 := r27");
    }

    {
        StatementList args;
        args.append(std::make_shared<Assign>(ebx, ebx));
        args.append(std::make_shared<Assign>(ebx, ecx));

        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->setArguments(args);

        call->eliminateDuplicateArgs();

        QCOMPARE(call->getNumArguments(), 1);
        QCOMPARE(call->getArguments().toString(), "   0 *v* r27 := r27");
    }

    {
        StatementList args;
        args.append(std::make_shared<Assign>(ebx, ebx));
        args.append(std::make_shared<Assign>(ecx, ebx));

        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->setArguments(args);

        call->eliminateDuplicateArgs();

        QCOMPARE(call->getNumArguments(), 2);
        QCOMPARE(call->getArguments().toString(), "   0 *v* r27 := r27,\t   0 *v* r25 := r27");
    }
}


void CallStatementTest::testDestProc()
{
    Prog prog("test", &m_project);
    LibProc *destProc = prog.getOrCreateLibraryProc("destProc");

    std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
    QCOMPARE(call->getDestProc(), nullptr);

    call->setDestProc(destProc);

    QCOMPARE(call->getDestProc(), destProc);
}


void CallStatementTest::testReturnAfterCall()
{
    std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
    QVERIFY(!call->isReturnAfterCall());
    call->setReturnAfterCall(false);
    QVERIFY(!call->isReturnAfterCall());
    call->setReturnAfterCall(true);
    QVERIFY(call->isReturnAfterCall());
}


void CallStatementTest::testIsChildless()
{
    Prog prog("test", &m_project);
    LibProc *destProc = prog.getOrCreateLibraryProc("destProc");

    UserProc *destUserProc = static_cast<UserProc *>(prog.getOrCreateFunction(Address(0x1000)));

    BasicBlock *bb = prog.getCFG()->createBB(BBType::Ret, createInsns(Address(0x1000), 1));
    IRFragment *frag = destUserProc->getCFG()->createFragment(FragType::Ret, createRTLs(Address(0x1000), 1, 0), bb);
    std::shared_ptr<ReturnStatement> calleeRet(new ReturnStatement);
    frag->getRTLs()->back()->append(calleeRet);

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        QVERIFY(call->isChildless());
    }

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->setDestProc(destProc);
        QVERIFY(!call->isChildless());
    }

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->setDestProc(destUserProc);
        QVERIFY(call->isChildless());
    }

    {
        destUserProc->setStatus(ProcStatus::FinalDone);
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->setDestProc(destUserProc);
        call->setCalleeReturn(calleeRet);

        QVERIFY(!call->isChildless());
    }
}


void CallStatementTest::testIsCallToMemOffset()
{
    std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
    QVERIFY(!call->isCallToMemOffset());

    call->setDest(Location::regOf(REG_X86_ECX));
    QVERIFY(!call->isCallToMemOffset());

    call->setDest(Address(0x2000));
    QVERIFY(!call->isCallToMemOffset());

    call->setDest(Location::memOf(Location::regOf(REG_X86_ECX)));
    QVERIFY(!call->isCallToMemOffset());

    call->setDest(Location::memOf(Const::get(0x2000)));
    QVERIFY(call->isCallToMemOffset());
}


void CallStatementTest::testAddDefine()
{
    const SharedExp ecx = Location::regOf(REG_X86_ECX);

    std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
    QVERIFY(call->getDefines().empty());

    call->addDefine(std::make_shared<ImplicitAssign>(ecx));
    QVERIFY(call->getDefines().size() == 1);
    QCOMPARE(call->getDefines().toString(), "   0 *v* r25 := -");
}


void CallStatementTest::testRemoveDefine()
{
    const SharedExp eax = Location::regOf(REG_X86_EAX);
    const SharedExp ecx = Location::regOf(REG_X86_ECX);

    std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
    QVERIFY(!call->removeDefine(ecx));

    call->addDefine(std::make_shared<ImplicitAssign>(ecx));
    QVERIFY(!call->removeDefine(eax));
    QCOMPARE(call->getDefines().toString(), "   0 *v* r25 := -");

    call->addDefine(std::make_shared<ImplicitAssign>(ecx));
    QVERIFY(call->removeDefine(ecx));
    QCOMPARE(call->getDefines().toString(), "   0 *v* r25 := -");

    QVERIFY(call->removeDefine(ecx));
    QCOMPARE(call->getDefines().toString(), "");
}


void CallStatementTest::testSetDefines()
{
    const SharedExp eax = Location::regOf(REG_X86_EAX);
    const SharedExp ecx = Location::regOf(REG_X86_ECX);

    std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));

    {
        StatementList defs;
        call->setDefines(defs);
        QVERIFY(call->getDefines().empty());
    }

    {
        StatementList defs;
        defs.append(std::make_shared<ImplicitAssign>(ecx));
        call->setDefines(defs);
        QVERIFY(!call->getDefines().empty());
        QCOMPARE(call->getDefines().toString(), "   0 *v* r25 := -");
    }

    {
        StatementList defs;
        call->setDefines(defs);
        QVERIFY(call->getDefines().empty());
    }
}


void CallStatementTest::testFindDefFor()
{
    const SharedExp eax = Location::regOf(REG_X86_EAX);
    const SharedExp ecx = Location::regOf(REG_X86_ECX);

    std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
    DefCollector *defCol = call->getDefCollector();

    QVERIFY(call->findDefFor(ecx) == nullptr);

    defCol->collectDef(std::make_shared<Assign>(ecx, eax));
    QVERIFY(call->findDefFor(ecx) != nullptr);
    QCOMPARE(*call->findDefFor(ecx), *eax);
}


void CallStatementTest::testCalcResults()
{
    QSKIP("TODO");
}


void CallStatementTest::testGetProven()
{
    Prog prog("test", &m_project);
    UserProc *srcProc = static_cast<UserProc *>(prog.getOrCreateFunction(Address(0x1000)));
    LibProc *destProc = prog.getOrCreateLibraryProc("destProc");
    destProc->setSignature(Signature::instantiate(Machine::X86, CallConv::C, "destProc"));

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x2000)));
        QVERIFY(call->getProven(Location::regOf(REG_X86_ECX)) == nullptr);
    }

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x2000)));
        call->setProc(srcProc);
        call->setDestProc(destProc);

        // it's an x86 signature, so ebx is preserved
        const SharedExp proven = call->getProven(Location::regOf(REG_X86_EBX));
        QVERIFY(proven != nullptr);
        QCOMPARE(*proven, *Location::regOf(REG_X86_EBX));
    }
}


void CallStatementTest::testLocaliseExp()
{
    QSKIP("TODO");
}


void CallStatementTest::testLocaliseComp()
{
    QSKIP("TODO");
}


void CallStatementTest::testBypassRef()
{
    QSKIP("TODO");
}


void CallStatementTest::testDoEllipsisProcessing()
{
    Prog prog("test", &m_project);
    UserProc *srcProc = static_cast<UserProc *>(prog.getOrCreateFunction(Address(0x1000)));
    LibProc *destProc = prog.getOrCreateLibraryProc("destProc");

    const SharedExp eax = Location::regOf(REG_X86_EAX);
    const SharedExp ecx = Location::regOf(REG_X86_ECX);

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        QVERIFY(!call->doEllipsisProcessing());
    }

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->setDestProc(destProc);
        call->setSignature(Signature::instantiate(Machine::X86, CallConv::C, "test"));
        QVERIFY(!call->doEllipsisProcessing()); // does not have ellipsis
    }

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->setDestProc(destProc);
        call->setSignature(Signature::instantiate(Machine::X86, CallConv::C, "objc_msgSend"));
        QVERIFY(!call->doEllipsisProcessing()); // does not have args
    }

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        std::shared_ptr<Signature> sig = Signature::instantiate(Machine::X86, CallConv::C, "test");
        sig->setHasEllipsis(true);

        call->setDestProc(destProc);
        call->setSignature(sig);

        QVERIFY(!call->doEllipsisProcessing());
    }

    {
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        std::shared_ptr<Signature> sig = Signature::instantiate(Machine::X86, CallConv::C, "test");
        sig->setHasEllipsis(true);

        StatementList args;
        args.append(std::make_shared<Assign>(ecx, ecx));
        args.append(std::make_shared<Assign>(eax, ecx));

        call->setArguments(args);
        call->setDestProc(destProc);
        call->setSignature(sig);

        QVERIFY(!call->doEllipsisProcessing());
    }

    {
        destProc->setName("printf");
        std::shared_ptr<Signature> sig = Signature::instantiate(Machine::X86, CallConv::C, "printf");
        sig->setHasEllipsis(true);
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));

        StatementList args;
        args.append(std::make_shared<Assign>(ecx, Unary::get(opAddrOf, RefExp::get(Location::memOf(eax), nullptr))));

        call->setArguments(args);
        call->setDestProc(destProc);
        call->setSignature(sig);

        QVERIFY(!call->doEllipsisProcessing());
    }

    {
        destProc->setName("sprintf");
        std::shared_ptr<Signature> sig = Signature::instantiate(Machine::X86, CallConv::C, "sprintf");
        sig->setHasEllipsis(true);
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));

        StatementList args;
        args.append(std::make_shared<Assign>(eax, ecx));
        args.append(std::make_shared<Assign>(ecx, RefExp::get(ecx, nullptr)));

        call->setArguments(args);
        call->setDestProc(destProc);
        call->setSignature(sig);

        QVERIFY(!call->doEllipsisProcessing());
    }

    {
        std::shared_ptr<Assign> def(new Assign(ecx, Const::get("foo")));

        destProc->setName("printf");
        std::shared_ptr<Signature> sig = Signature::instantiate(Machine::X86, CallConv::C, "printf");
        sig->setHasEllipsis(true);
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));

        StatementList args;
        args.append(std::make_shared<Assign>(ecx, RefExp::get(ecx, def)));

        call->setArguments(args);
        call->setDestProc(destProc);
        call->setSignature(sig);

        QVERIFY(call->doEllipsisProcessing());
    }

    {
        std::shared_ptr<Assign> def(new Assign(ecx, Const::get(5)));

        destProc->setName("printf");
        std::shared_ptr<Signature> sig = Signature::instantiate(Machine::X86, CallConv::C, "printf");
        sig->setHasEllipsis(true);
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));

        StatementList args;
        args.append(std::make_shared<Assign>(ecx, RefExp::get(ecx, def)));

        call->setArguments(args);
        call->setDestProc(destProc);
        call->setSignature(sig);

        QVERIFY(!call->doEllipsisProcessing());
    }

    {
        std::shared_ptr<ImplicitAssign> def(new ImplicitAssign(ecx));

        destProc->setName("printf");
        std::shared_ptr<Signature> sig = Signature::instantiate(Machine::X86, CallConv::C, "printf");
        sig->setHasEllipsis(true);
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));

        StatementList args;
        args.append(std::make_shared<Assign>(ecx, RefExp::get(ecx, def)));

        call->setArguments(args);
        call->setDestProc(destProc);
        call->setSignature(sig);

        QVERIFY(!call->doEllipsisProcessing());
    }

    // TODO Test if def is a phi

    {
        destProc->setName("printf");
        std::shared_ptr<Signature> sig = Signature::instantiate(Machine::X86, CallConv::C, "printf");
        sig->setHasEllipsis(true);
        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));

        StatementList args;
        args.append(std::make_shared<Assign>(ecx, Const::get("foo")));

        call->setArguments(args);
        call->setDestProc(destProc);
        call->setSignature(sig);

        QVERIFY(call->doEllipsisProcessing());
    }

    {
        destProc->setName("printf");
        std::shared_ptr<Signature> sig = Signature::instantiate(Machine::X86, CallConv::C, "printf");
        sig->setHasEllipsis(true);

        sig->addParameter(Location::param("fmt"), PointerType::get(CharType::get()));

        StatementList args;
        args.append(std::make_shared<Assign>(Location::param("fmt"),
            Const::get("%d %i %u %o %x %X %f %F %e %E %g %G %a %A %c %s %p %%")));

        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->setArguments(args);
        call->setDestProc(destProc);
        call->setSignature(sig);
        call->setProc(srcProc);

        QVERIFY(call->doEllipsisProcessing());

        const QString expected =
            "   0 *v* fmt := \"%d %i %u %o %x %X %f %F %e %E %g %G %a %A %c %s %p %%\",\t"
            "   0 *i32* m[r28{-} + 8] := m[r28{-} + 8]{-},\t"    // %d
            "   0 *i32* m[r28{-} + 12] := m[r28{-} + 12]{-},\t"  // %i
            "   0 *u32* m[r28{-} + 16] := m[r28{-} + 16]{-},\t"  // %u
            "   0 *u32* m[r28{-} + 20] := m[r28{-} + 20]{-},\t"  // %o
            "   0 *u32* m[r28{-} + 24] := m[r28{-} + 24]{-},\t"  // %x
            "   0 *u32* m[r28{-} + 28] := m[r28{-} + 28]{-},\t"  // %X
            "   0 *f64* m[r28{-} + 32] := m[r28{-} + 32]{-},\t"  // %f (f64 beause printf)
            "   0 *f64* m[r28{-} + 36] := m[r28{-} + 36]{-},\t"  // %F (f64 beause printf)
            "   0 *f64* m[r28{-} + 40] := m[r28{-} + 40]{-},\t"  // %e
            "   0 *f64* m[r28{-} + 44] := m[r28{-} + 44]{-},\t"  // %E
            "   0 *f64* m[r28{-} + 48] := m[r28{-} + 48]{-},\t"  // %g
            "   0 *f64* m[r28{-} + 52] := m[r28{-} + 52]{-},\t"  // %G
            "   0 *f64* m[r28{-} + 56] := m[r28{-} + 56]{-},\t"  // %a
            "   0 *f64* m[r28{-} + 60] := m[r28{-} + 60]{-},\t"  // %A
            "   0 *c* m[r28{-} + 64] := m[r28{-} + 64]{-},\t"    // %c
            "   0 *[c]** m[r28{-} + 68] := m[r28{-} + 68]{-},\t" // %s
            "   0 *v** m[r28{-} + 72] := m[r28{-} + 72]{-}";     // %p

        QCOMPARE(call->getNumArguments(), 18);
        QCOMPARE(call->getArguments().toString(), expected);
    }

    {
        destProc->setName("scanf");
        std::shared_ptr<Signature> sig = Signature::instantiate(Machine::X86, CallConv::C, "scanf");
        sig->setHasEllipsis(true);

        sig->addParameter(Location::param("fmt"), PointerType::get(CharType::get()));

        StatementList args;
        args.append(std::make_shared<Assign>(Location::param("fmt"),
            Const::get("%d %i %u %o %x %X %f %F %e %E %g %G %a %A %c %s %p %%")));

        std::shared_ptr<CallStatement> call(new CallStatement(Address(0x1000)));
        call->setArguments(args);
        call->setDestProc(destProc);
        call->setSignature(sig);
        call->setProc(srcProc);

        QVERIFY(call->doEllipsisProcessing());

        const QString expected =
            "   0 *v* fmt := \"%d %i %u %o %x %X %f %F %e %E %g %G %a %A %c %s %p %%\",\t"
            "   0 *i32** m[r28{-} + 8] := m[r28{-} + 8]{-},\t"    // %d
            "   0 *i32** m[r28{-} + 12] := m[r28{-} + 12]{-},\t"  // %i
            "   0 *u32** m[r28{-} + 16] := m[r28{-} + 16]{-},\t"  // %u
            "   0 *u32** m[r28{-} + 20] := m[r28{-} + 20]{-},\t"  // %o
            "   0 *u32** m[r28{-} + 24] := m[r28{-} + 24]{-},\t"  // %x
            "   0 *f32** m[r28{-} + 28] := m[r28{-} + 28]{-},\t"  // %f (f32 beause scanf)
            "   0 *f32** m[r28{-} + 32] := m[r28{-} + 32]{-},\t"  // %e
            "   0 *f32** m[r28{-} + 36] := m[r28{-} + 36]{-},\t"  // %g
            "   0 *f32** m[r28{-} + 40] := m[r28{-} + 40]{-},\t"  // %a
            "   0 *c** m[r28{-} + 44] := m[r28{-} + 44]{-},\t"    // %c
            "   0 *[c]*** m[r28{-} + 48] := m[r28{-} + 48]{-},\t" // %s
            "   0 *v*** m[r28{-} + 52] := m[r28{-} + 52]{-}";     // %p

        QCOMPARE(call->getNumArguments(), 13);
        QCOMPARE(call->getArguments().toString(), expected);
    }
}


void CallStatementTest::testTryConvertToDirect()
{
    QSKIP("TODO");
}


QTEST_GUILESS_MAIN(CallStatementTest)
