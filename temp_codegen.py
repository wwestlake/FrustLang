import re
with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\Codegen.h', 'r') as f:
    content = f.read()

# Add currentNamespace to Codegen class
content = content.replace(
    'class Codegen {',
    'class Codegen {\npublic:\n    std::string currentNamespace;'
)

# Update compilePath
content = re.sub(
    r'case ExprKind::Path: \{\n\s*std::string fullName = expr->pathSegments\.front\(\);\n\s*for \(size_t i = 1; i < expr->pathSegments\.size\(\); \+\+i\) fullName \+= "::" \+ expr->pathSegments\[i\];\n\s*auto it = namedValues\.find\(fullName\);\n\s*if \(it != namedValues\.end\(\)\) return it->second;',
    r'case ExprKind::Path: {\n                std::string fullName = expr->pathSegments.front();\n                for (size_t i = 1; i < expr->pathSegments.size(); ++i) fullName += "::" + expr->pathSegments[i];\n                auto it = namedValues.find(fullName);\n                if (it != namedValues.end()) return it->second;\n                if (!currentNamespace.empty()) {\n                    it = namedValues.find(currentNamespace + "::" + fullName);\n                    if (it != namedValues.end()) return it->second;\n                }',
    content
)

# Update resolveType to check namespace
content = re.sub(
    r'auto it = namedTypes\.find\(typeAnnotation\.text\);\n\s*if \(it != namedTypes\.end\(\)\) return it->second;',
    r'auto it = namedTypes.find(typeAnnotation.text);\n        if (it != namedTypes.end()) return it->second;\n        if (!currentNamespace.empty()) {\n            it = namedTypes.find(currentNamespace + "::" + typeAnnotation.text);\n            if (it != namedTypes.end()) return it->second;\n        }',
    content
)

with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\Codegen.h', 'w') as f:
    f.write(content)
