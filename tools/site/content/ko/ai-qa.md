---
status: to-be-translated
title: AI Q&A
nav: AI Q&A
description: Ask a question about the app without leaving it, and get an answer written from this documentation.
---
**Ask AI** opens AI Q&A at the side of the window. You type a question about the app, and AI writes an answer from this documentation, with links to the pages it used.

## Open and close it {#open}

1. Click **Ask AI** in the top bar, next to the **Documentation** button. Hold the pointer over a button to see its name.
   => A panel opens at the right edge of the window. The picture and the sidebar move over to make room, so the panel covers nothing.
2. Type a question and press ((Enter)), or click one of the questions under **Try asking**.
   => The answer appears in the panel.
3. To close the panel, press ((Esc)) in the panel, click **Ask AI** again, or click the panel's **×**.

The first opening takes a moment, because the app starts the panel only then. The panel shows **Loading AI Q&A…** until it is ready.

Closing only hides the panel. Your conversation is still there when you open it again, until you close the app.

## What it can answer {#answers}

AI Q&A knows this documentation: the downloads, every setting, the live camera, videos, the messages the app shows and what to do about them. It answers in the interface's language.

- **It cannot see the app.** It knows nothing about your settings, your picture or your screen. Tell it what you see, and name controls and messages the way the app writes them.
- **Answers can be wrong.** AI writes them, and the panel says so too. Before you rely on an answer, open the page it links to and check.
- **Links open in your browser.** A link to a page of this documentation opens that page in the interface's language.
- **It follows the interface.** A change of theme shows at once. A change of language opens the panel again in that language, with a new conversation.

Mintlify's own controls in the panel, such as **Clear chat** and **Found results for …**, stay in English.

## It works online {#online}

AI Q&A runs on the servers of [Mintlify](https://mintlify.com), a documentation service, not on your computer. So it needs an internet connection. When the panel cannot load, it says **AI Q&A could not be loaded** and offers **Try again**.

It can also pause for a while. Mintlify gives the project a monthly allowance, and everyone who uses the app shares it. When the allowance runs out, AI Q&A stops answering until the next month.

So please ask where it helps most: when you don't know which page to read. To look up one control, [All settings](settings.html) or the search at the top of these pages is often quicker. This documentation is always there, with or without AI Q&A.

## What is sent, and what stays {#privacy}

- **Nothing starts before you open it.** The app starts the panel at your first click on **Ask AI**. Until then, nothing is sent.
- **What you type goes to Mintlify**, which writes the answer. Leave out anything private, such as an MCP key or a folder path with your name in it. Mintlify also receives usage events from the panel, and checks questions with hCaptcha to keep bots out.
- **Your files stay on your computer.** The app never sends your pictures, videos, settings or library. If the panel offers to attach a file, whatever you attach or paste there goes to Mintlify too.
- **The panel keeps its cookies and storage** in `%LOCALAPPDATA%\VRChatDLSS5Cam\webview2`.
- **The app's log** notes the HTTP status and the length of each answer. Your questions and the answers' text are never written to it.

All the details: [Privacy and licences](privacy.html#network).

## Without WebView2, and on Linux {#browser}

The panel needs the **Microsoft Edge WebView2 Runtime**, a Microsoft component that shows web pages inside apps. Windows 11 has it, and so do most Windows 10 computers.

<!-- if askWidget -->
Where it is missing or cannot start, and in the [Linux package](linux.html), **Ask AI** opens the AI Q&A of this documentation in your browser instead. The app says **AI Q&A opened in your browser**. The answers come from the same pages.
<!-- else -->
Where it is missing or cannot start, and in the [Linux package](linux.html), **Ask AI** opens this documentation in your browser instead.
<!-- endif -->

To have the panel inside the app, install the WebView2 Runtime from [Microsoft's download page](https://developer.microsoft.com/microsoft-edge/webview2/), then start the app again.
