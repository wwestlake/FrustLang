# LdFn - LagDaemon Functional Language Specification

**LdFn** is a pure, expression-oriented functional language designed for high-performance JIT execution via LLVM OrcJIT.

---

## 1. Lexical Tokens

### Keywords
* `let`, `in`, `fn`, `match`, `with`, `if`, `then`, `else`, `type`, `perform`, `handle`

### Literals
* `INT_LIT` : Integer literal (e.g. `42`, `-7`)
* `FLOAT_LIT` : Floating point literal (e.g. `3.14159`)
* `BOOL_LIT` : `true` | `false`
* `STRING_LIT` : Double-quoted string literal (e.g. `"hello world"`)
* `IDENT` : Identifier matching `[a-zA-Z_][a-zA-Z0-9_]*`
* `CONSTR_IDENT` : Constructor identifier matching `[A-Z][a-zA-Z0-9_]*` (e.g., `Some`, `None`, `Cons`, `Nil`)

### Operators & Punctuation
* Arrow: `->`, `=>`
* Assignment / Binding: `=`
* Arithmetic: `+`, `-`, `*`, `/`, `%`
* Comparison: `==`, `!=`, `<`, `<=`, `>`, `>=`
* Delimiters: `(`, `)`, `{`, `}`, `[`, `]`, `,`, `:`, `;`, `|`

---

## 2. EBNF Syntactic Grammar

```ebnf
Program ::= { TopLevelDecl } ;

TopLevelDecl ::= TypeDecl | FunctionDecl | LetBinding ;

(* Type Declarations (Algebraic Data Types) *)
TypeDecl ::= "type" CONSTR_IDENT [ TypeParams ] "=" [ "|" ] Variant { "|" Variant } ;
Variant ::= CONSTR_IDENT [ "(" TypeList ")" ] ;
TypeList ::= Type { "," Type } ;

(* Function Declarations *)
FunctionDecl ::= "fn" IDENT "(" [ ParamList ] ")" [ "->" Type ] "=" Expr ;
ParamList ::= Param { "," Param } ;
Param ::= IDENT [ ":" Type ] ;

(* Expression Hierarchy *)
Expr ::= LetExpr
       | MatchExpr
       | IfExpr
       | LambdaExpr
       | PerformExpr
       | HandleExpr
       | BinaryExpr ;

LetExpr ::= "let" ( IDENT | Pattern ) "=" Expr "in" Expr ;

IfExpr ::= "if" Expr "then" Expr "else" Expr ;

LambdaExpr ::= "fn" "(" [ ParamList ] ")" "=>" Expr ;

MatchExpr ::= "match" Expr "with" [ "|" ] MatchCase { "|" MatchCase } ;
MatchCase ::= Pattern "=>" Expr ;

Pattern ::= CONSTR_IDENT [ "(" PatternList ")" ]
          | IDENT
          | Literal
          | "_" ;
PatternList ::= Pattern { "," Pattern } ;

PerformExpr ::= "perform" CONSTR_IDENT "(" [ ExprList ] ")" ;

HandleExpr ::= "handle" Expr "with" [ "|" ] HandleCase { "|" HandleCase } ;
HandleCase ::= "effect" CONSTR_IDENT "(" [ ParamList ] ")" "=>" Expr ;

BinaryExpr ::= UnaryExpr { BinaryOp UnaryExpr } ;
UnaryExpr  ::= [ "-" | "!" ] PrimaryExpr ;

PrimaryExpr ::= Literal
              | IDENT
              | CONSTR_IDENT [ "(" [ ExprList ] ")" ]
              | CallExpr
              | "(" Expr ")" ;

CallExpr ::= PrimaryExpr "(" [ ExprList ] ")" ;
ExprList ::= Expr { "," Expr } ;

Literal ::= INT_LIT | FLOAT_LIT | BOOL_LIT | STRING_LIT ;
```

---

## 3. Abstract Syntax Tree (AST) Hierarchy (C++ Data Structures)

```cpp
// AST Base Node
struct ASTNode {
    virtual ~ASTNode() = default;
};

// Expressions
struct ExprNode : public ASTNode {};

struct IntLitExpr : public ExprNode { int64_t value; };
struct FloatLitExpr : public ExprNode { double value; };
struct BoolLitExpr : public ExprNode { bool value; };
struct VarExpr : public ExprNode { std::string name; };

struct BinaryExpr : public ExprNode {
    std::string op;
    std::unique_ptr<ExprNode> lhs;
    std::unique_ptr<ExprNode> rhs;
};

struct LambdaExpr : public ExprNode {
    std::vector<std::string> params;
    std::unique_ptr<ExprNode> body;
};

struct AppExpr : public ExprNode { // Function Call
    std::unique_ptr<ExprNode> fn;
    std::vector<std::unique_ptr<ExprNode>> args;
};

struct LetExpr : public ExprNode {
    std::string varName;
    std::unique_ptr<ExprNode> valExpr;
    std::unique_ptr<ExprNode> bodyExpr;
};

struct MatchCase {
    std::unique_ptr<ASTNode> pattern;
    std::unique_ptr<ExprNode> body;
};

struct MatchExpr : public ExprNode {
    std::unique_ptr<ExprNode> target;
    std::vector<MatchCase> cases;
};
```

---

## 4. Sample LdFn Code

### Example 1: Recursive Fibonacci with Tail Call Optimization
```ldfn
fn fib(n: Int, a: Int, b: Int) -> Int =
    if n == 0 then
        a
    else
        fib(n - 1, b, a + b)
```

### Example 2: Algebraic Data Types & Pattern Matching
```ldfn
type Option = | Some(Int) | None

fn unwrap_or(opt: Option, fallback: Int) -> Int =
    match opt with
    | Some(val) => val
    | None      => fallback
```

### Example 3: First-Class Lambda Closures & Currying
```ldfn
fn make_adder(x: Int) -> (Int -> Int) =
    fn(y: Int) => x + y

let add10 = make_adder(10) in
add10(32)  // Evaluates to 42
```
