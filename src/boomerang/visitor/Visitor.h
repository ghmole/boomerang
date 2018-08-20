#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#pragma once

/**
 * \file      visitor.h
 * \details   Provides the definition for the various visitor and modifier classes.
 *            These classes sometimes are associated with Statement and Exp classes, so they are
 * here to avoid
 *            \#include problems, to make exp.cpp and statement.cpp a little less huge.
 *            The main advantage is that they are quick and easy to implement (once you get used to
 * them), and it avoids having to declare methods in every Statement or Exp subclass TOP LEVEL
 * CLASSES:
 *     Class name    |  description
 * ------------------|-----------------------------
 * ExpVisitor        | (visit expressions)
 * StmtVisitor       | (visit statements)
 * StmtExpVisitor    | (visit expressions in statements)
 * ExpModifier       | (modify expressions)
 * SimpExpModifier   | (simplifying expression modifier)
 * StmtModifier      | (modify expressions in statements; not abstract)
 * StmtPartModifier  | (as above with special case for whole of LHS)
 *
 * \note There are separate Visitor and Modifier classes. Visitors are more suited for searching:
 * they have the capability of stopping the recursion, but can't change the class of a top level
 * expression. Visitors can also override (prevent) the usual recursing to child objects. Modifiers
 * always recurse to the end, and the ExpModifiers' visit function returns an Exp* so that the top
 * level expression can change class (e.g. RefExp to Binary). The accept() functions (in the target
 * classes) are always the same for all visitors; they encapsulate where the visitable parts of a
 * Statement or expression are. The visit() functions contain the logic of the
 * search/modify/whatever.  Often only a few visitor functions have to do anything. Unfortunately,
 * the visit functions are members of the Visitor (or Modifier) classes, and so have to use public
 * functions of the target classes.
 */
