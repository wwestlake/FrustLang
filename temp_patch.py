with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\Codegen.h', 'r') as f:
    code = f.read()

replacement = '''
            case ExprKind::Unquote:
                std::cerr << "frust: codegen error: unquote() is only valid inside quote { ... }\\n";
                hadCodegenError = true;
                return nullptr;

            case ExprKind::Cast: {
                llvm::Value* val = compileExpr(expr->lhs);
                if (!val) return nullptr;
                llvm::Type* targetTy = resolveType(expr->typeAnnotation);
                if (!targetTy) {
                    std::cerr << "frust: codegen error: invalid cast type\\n";
                    hadCodegenError = true;
                    return nullptr;
                }
                if (val->getType()->isPointerTy() && targetTy->isPointerTy()) {
                    return val;
                }
                return coerceToType(val, targetTy);
            }

            default:
                std::cerr << "frust: codegen does not support this expression kind yet\\n";
                hadCodegenError = true;
                return nullptr;
'''

old = '''
            case ExprKind::Unquote:
                std::cerr << "frust: codegen error: unquote() is only valid inside quote { ... }\\n";
                return nullptr;

            default:
                std::cerr << "frust: codegen does not support this expression kind yet\\n";
                return nullptr;
'''

if old in code:
    code = code.replace(old, replacement)
    with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\Codegen.h', 'w') as f:
        f.write(code)
    print("PATCHED")
else:
    print("NOT FOUND")
