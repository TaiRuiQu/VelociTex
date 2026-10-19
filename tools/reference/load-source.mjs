import fs from 'node:fs';
import path from 'node:path';
import Module, {createRequire} from 'node:module';
import ts from 'typescript';
import {sourceRoot,lock} from './acquire.mjs';

// Development-only loader: execute the locked source's real transitive import
// graph after TypeScript AST transformation. Dynamic loops and aliases execute
// normally; no regular expression is used to guess registered names.
export function loadSource() {
  const require=createRequire(import.meta.url);
  const registrations=[];
  const moduleSources=[];
  const propertySources=new Map();
  let capture=true;
  globalThis.__VERSION__=lock.npm.version;
  globalThis.__velocitexRegister=(kind,source,fn,...args)=>{
    if(capture) registrations.push({kind,source,args});
    return fn(...args);
  };
  const kinds={defineFunction:'function',defineFunctionBuilders:'builder',defineEnvironment:'environment',defineSymbol:'symbol',defineMacro:'macro'};
  const transformer=context=>sourceFile=>{
    const relative=path.relative(sourceRoot,sourceFile.fileName).replaceAll('\\','/');
    const sourceOf=node=>({file:relative,line:sourceFile.getLineAndCharacterOfPosition(node.getStart(sourceFile)).line+1});
    const visitor=node=>{
      if(ts.isVariableDeclaration(node)&&ts.isIdentifier(node.name)&&['SETTINGS_SCHEMA','implicitCommands'].includes(node.name.text)&&node.initializer&&ts.isObjectLiteralExpression(node.initializer)) {
        for(const prop of node.initializer.properties) if(prop.name) propertySources.set(`${node.name.text}:${prop.name.text}`,sourceOf(prop));
      }
      if(ts.isCallExpression(node)&&ts.isIdentifier(node.expression)&&kinds[node.expression.text]) {
        const transformed=ts.visitEachChild(node,visitor,context);
        return context.factory.createCallExpression(context.factory.createPropertyAccessExpression(context.factory.createIdentifier('globalThis'),'__velocitexRegister'),undefined,[context.factory.createStringLiteral(kinds[node.expression.text]),context.factory.createObjectLiteralExpression([context.factory.createPropertyAssignment('file',context.factory.createStringLiteral(relative)),context.factory.createPropertyAssignment('line',context.factory.createNumericLiteral(sourceOf(node).line))]),transformed.expression,...transformed.arguments]);
      }
      return ts.visitEachChild(node,visitor,context);
    };
    return ts.visitNode(sourceFile,visitor);
  };
  const originalJs=Module._extensions['.js'];
  const originalTs=Module._extensions['.ts'];
  const compile=(module,file)=>{
    if(!file.startsWith(sourceRoot+path.sep)) return originalJs(module,file);
    moduleSources.push(path.relative(sourceRoot,file).replaceAll('\\','/'));
    const result=ts.transpileModule(fs.readFileSync(file,'utf8'),{fileName:file,compilerOptions:{target:ts.ScriptTarget.ES2022,module:ts.ModuleKind.CommonJS,esModuleInterop:true},transformers:{before:[transformer]}});
    module._compile(result.outputText,file);
  };
  Module._extensions['.ts']=compile;
  Module._extensions['.js']=compile;
  try {
    const katex=require(path.join(sourceRoot,'katex.ts')).default;
    const functions=require(path.join(sourceRoot,'src/defineFunction.ts'));
    const environments=require(path.join(sourceRoot,'src/defineEnvironment.ts'));
    const macros=require(path.join(sourceRoot,'src/defineMacro.ts'))._macros;
    const symbols=require(path.join(sourceRoot,'src/symbols.ts')).default;
    const implicitCommands=require(path.join(sourceRoot,'src/MacroExpander.ts')).implicitCommands;
    capture=false;
    return {katex,functions,environments,macros,symbols,implicitCommands,registrations,moduleSources,propertySources};
  } finally {
    Module._extensions['.js']=originalJs;
    if(originalTs) Module._extensions['.ts']=originalTs; else delete Module._extensions['.ts'];
  }
}
