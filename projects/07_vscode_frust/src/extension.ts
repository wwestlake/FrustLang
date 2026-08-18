import * as vscode from 'vscode';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

export function activate(context: vscode.ExtensionContext) {
    const config = vscode.workspace.getConfiguration('frust');
    const serverPath = config.get<string>('languageServerPath', 'frust_lsp');

    const serverOptions: ServerOptions = {
        command: serverPath,
        transport: TransportKind.stdio,
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'frust' }],
    };

    client = new LanguageClient('frust', 'Frust Language Server', serverOptions, clientOptions);

    client.start().catch((err: Error) => {
        // Most common failure: frust_lsp isn't on PATH. A real "download
        // the toolchain" link belongs here once the release/CI system
        // exists (not built yet - see project notes) - a fake or
        // not-yet-real URL would be worse than no link at all.
        vscode.window.showErrorMessage(
            `Frust language server ("${serverPath}") could not be started. ` +
            `Make sure frust_lsp is installed and on your PATH, or set ` +
            `"frust.languageServerPath" in your VSCode settings to its ` +
            `full path. (${err.message})`
        );
    });

    context.subscriptions.push({ dispose: () => client?.stop() });
}

export function deactivate(): Thenable<void> | undefined {
    return client?.stop();
}
