#include <gtk/gtk.h>
#include <gio/gio.h>
#include <webkit2/webkit2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global references needed by the file watcher callback
WebKitWebView *web_view;
char *target_file_path;

// ---------------------------------------------------------
// 1. Read File Helper
// ---------------------------------------------------------
char* read_file_contents(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buffer = malloc(length + 1);
    if (buffer) {
        fread(buffer, 1, length, f);
        buffer[length] = '\0';
    }
    fclose(f);
    return buffer;
}

// ---------------------------------------------------------
// 2. The HTML/JS Template
// ---------------------------------------------------------
const char *get_html_template() {
    return "<!DOCTYPE html>\n"
           "<html lang=\"en\">\n"
           "<head>\n"
           "    <meta charset=\"UTF-8\">\n"
           "    <title>jdft! Markdown</title>\n"
           "    <style>\n"
           "        :root { --bg: #1e1e1e; --text: #d4d4d4; --accent: #569cd6; }\n"
           "        body {\n"
           "            background-color: var(--bg); color: var(--text);\n"
           "            font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Helvetica, Arial, sans-serif;\n"
           "            line-height: 1.6; max-width: 850px; margin: 0 auto; padding: 40px 20px;\n"
           "            scrollbar-width: none; \n"
           "        }\n"
           "        body::-webkit-scrollbar { display: none; }\n"
           "        pre { background: #2d2d2d; padding: 16px; border-radius: 6px; overflow-x: auto; }\n"
           "        code { font-family: monospace; background: #2d2d2d; padding: 2px 4px; border-radius: 3px; }\n"
           "        a { color: var(--accent); text-decoration: none; }\n"
           "        .mermaid { background: #fff; padding: 10px; border-radius: 6px; }\n"
           "    </style>\n"
           "    <script src=\"https://cdn.jsdelivr.net/npm/marked/marked.min.js\"></script>\n"
           "    <script type=\"module\">\n"
           "        import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';\n"
           "        mermaid.initialize({ startOnLoad: false, theme: 'default' });\n"
           "        window.mermaidEngine = mermaid;\n"
           "    </script>\n"
           "    <script>\n"
           "        window.renderMarkdown = async function(mdText) {\n"
           "            const scrollPos = window.scrollY;\n"
           "            document.getElementById('content').innerHTML = marked.parse(mdText);\n"
           "            if (window.mermaidEngine) {\n"
           "                const mermaidBlocks = document.querySelectorAll('code.language-mermaid');\n"
           "                for (let i = 0; i < mermaidBlocks.length; i++) {\n"
           "                    const block = mermaidBlocks[i];\n"
           "                    const pre = block.parentElement;\n"
           "                    const uniqueId = 'mermaid-' + Date.now() + '-' + i;\n"
           "                    try {\n"
           "                        const { svg } = await window.mermaidEngine.render(uniqueId, block.textContent);\n"
           "                        const div = document.createElement('div');\n"
           "                        div.className = 'mermaid';\n"
           "                        div.innerHTML = svg;\n"
           "                        pre.parentNode.replaceChild(div, pre);\n"
           "                    } catch (e) { console.error(e); }\n"
           "                }\n"
           "            }\n"
           "            window.scrollTo(0, scrollPos);\n"
           "        };\n"
           "        let gPressed = false;\n"
           "        document.addEventListener('keydown', (e) => {\n"
           "            if (e.ctrlKey || e.metaKey) return;\n"
           "            const scrollAmount = 75;\n"
           "            if (e.key === 'j') { window.scrollBy({ top: scrollAmount, behavior: 'auto' }); gPressed = false; }\n"
           "            else if (e.key === 'k') { window.scrollBy({ top: -scrollAmount, behavior: 'auto' }); gPressed = false; }\n"
           "            else if (e.key === 'G') { window.scrollTo({ top: document.body.scrollHeight, behavior: 'auto' }); gPressed = false; }\n"
           "            else if (e.key === 'g') {\n"
           "                if (gPressed) { window.scrollTo({ top: 0, behavior: 'auto' }); gPressed = false; }\n"
           "                else { gPressed = true; setTimeout(() => gPressed = false, 500); }\n"
           "            }\n"
           "            else if (e.key === 'q') {\n"
           "                 // Send a message back to C to close the app\n"
           "                 window.webkit.messageHandlers.quitHandler.postMessage(\"quit\");\n"
           "            }\n"
           "            else { gPressed = false; }\n"
           "        });\n"
           "    </script>\n"
           "</head>\n"
           "<body>\n"
           "    <div id=\"content\">Loading...</div>\n"
           "</body>\n"
           "</html>";
}

// ---------------------------------------------------------
// 3. The JS Injector
// ---------------------------------------------------------
// This takes a raw string, escapes it properly for JS, and sends it to WebKit
void inject_markdown_to_js(const char *raw_md) {
    if (!raw_md) return;

    // Create a JSON encoded string safely using Glib
    char *escaped_json = g_strescape(raw_md, NULL);
    char *js_command = g_strdup_printf("window.renderMarkdown(\"%s\")", escaped_json);

    // Execute the JS in the WebKit view
    webkit_web_view_evaluate_javascript(web_view, js_command, -1, NULL, NULL, NULL, NULL, NULL);

    g_free(js_command);
    g_free(escaped_json);
}

// ---------------------------------------------------------
// 4. File Watcher Callback
// ---------------------------------------------------------
// This runs whenever GTK detects a change to our target file
void on_file_changed(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer user_data) {
    // Only trigger on the "CHANGES_DONE_HINT" event to avoid reading a half-written file during save
    if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {
        char *content = read_file_contents(target_file_path);
        if (content) {
            inject_markdown_to_js(content);
            free(content);
        }
    }
}

// ---------------------------------------------------------
// 5. JavaScript 'q' Quit Handler
// ---------------------------------------------------------
// This catches the 'window.webkit.messageHandlers.quitHandler' message from JS
void on_js_quit_message(WebKitUserContentManager *manager, WebKitJavascriptResult *js_result, gpointer user_data) {
    gtk_main_quit();
}

// ---------------------------------------------------------
// 5.5 Load Changed Callback (Initial Render)
// ---------------------------------------------------------
void on_load_changed(WebKitWebView *v, WebKitLoadEvent e, gpointer data) {
    if (e == WEBKIT_LOAD_FINISHED) {
        inject_markdown_to_js((char*)data);
        free(data); // Free the initial content after injection
    }
}

// ---------------------------------------------------------
// 6. Main Application Entry
// ---------------------------------------------------------
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: fkgmd <file.md>\n");
        return 1;
    }

    target_file_path = argv[1];

    // Disable Nvidia/WebKit compositor crashing (The Linux fix we discussed)
    setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 1);

    gtk_init(&argc, &argv);

    // Create the main GTK Window
    GtkWidget *main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(main_window), 1024, 768);

    char title[256];
    snprintf(title, sizeof(title), "jdft! - %s", target_file_path);
    gtk_window_set_title(GTK_WINDOW(main_window), title);

    // Bind the OS window close (X button) to cleanly exit
    g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Setup WebKit View
    web_view = WEBKIT_WEB_VIEW(webkit_web_view_new());
    gtk_container_add(GTK_CONTAINER(main_window), GTK_WIDGET(web_view));

    // Setup the JS-to-C Bridge (for the 'q' quit keybinding)
    WebKitUserContentManager *manager = webkit_web_view_get_user_content_manager(web_view);
    webkit_user_content_manager_register_script_message_handler(manager, "quitHandler");
    g_signal_connect(manager, "script-message-received::quitHandler", G_CALLBACK(on_js_quit_message), NULL);

    // Load the HTML Template
    webkit_web_view_load_html(web_view, get_html_template(), NULL);

    // Initialize File Watcher GFile *gfile = g_file_new_for_path(target_file_path);
    GFile *target_gfile = g_file_new_for_path(target_file_path);
    GFileMonitor *monitor = g_file_monitor_file(target_gfile, G_FILE_MONITOR_NONE, NULL, NULL);
    g_signal_connect(monitor, "changed", G_CALLBACK(on_file_changed), NULL);

    // Initial Render
    char *initial_content = read_file_contents(target_file_path);
    if (initial_content) {
        // We must wait for the load-changed signal to ensure the HTML is loaded before injecting JS
        g_signal_connect(web_view, "load-changed", G_CALLBACK(on_load_changed), initial_content);
    }

    // Show the window and start the GTK event loop
    gtk_widget_show_all(main_window);
    gtk_main();

    // Cleanup
    g_object_unref(monitor);
    g_object_unref(target_gfile);

    return 0;
}
