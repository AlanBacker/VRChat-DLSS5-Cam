---
title: AI Q&A
nav: AI Q&A
description: Ask a question about the app without leaving it, and get an answer written from this documentation.
---
**Ask AI** opens AI Q&A at the side of the window. You type a question about the app, and AI writes an answer from this documentation, with links to the pages it used.

## Open and close it {#open}

1. Click **Ask AI** at the top right of the window: the speech bubble with "AI", just right of the book with "?" (**Documentation**). Hold the pointer over a button to see what it does.
   => A panel opens at the right edge of the window. The picture and the sidebar move over to make room, so the panel covers nothing.
2. Type a question and press ((Enter)), or click one of the questions under **Try asking**.
   => The answer appears in the panel.
3. To close the panel, press ((Esc)) in the panel, click **Ask AI** again, or click the panel's **×**.

The first opening takes a moment, because the app starts the panel only then. The panel shows **Loading AI Q&A…** until it is ready.

Closing only hides the panel. Your conversation is still there when you open it again, until you close the app.

To make the panel wider or narrower, drag its left edge; a line lights up under the pointer. The panel stays at least 16 font sizes wide and always leaves the picture some room. The app remembers the width.

## What it can answer {#answers}

AI Q&A knows this documentation: the downloads, every setting, the live camera, videos, the messages the app shows and what to do about them. It answers in the interface's language.

- **It cannot see the app.** It knows nothing about your settings, your picture or your screen. Tell it what you see, and name controls and messages the way the app writes them.
- **Answers can be wrong.** AI writes them, and the line at the top of the panel says so too. Before you rely on an answer, open the page it links to and check.
- **Links open in your browser.** A link to a page of this documentation opens that page in the interface's language.
- **It follows the interface.** A change of theme shows at once. A change of language opens the panel again in that language, with a new conversation.

Mintlify's own controls in the panel, such as **Clear chat** and **Found results for …**, stay in English.

## From an answer to the control {#jump}

When an answer names a control or a section of the app the way the interface writes it, in bold, as code or in quotes, the name gets a dotted underline. It takes you to that place in the app:

- **Click the name.** The sidebar comes out if it was hidden, the section opens, the sidebar glides to the control, and a ring lights up around it for a moment. Nothing is set or changed: the value is still yours to choose.
- **Hold the pointer over the name** for a card with two choices. **Show in the app** does the same as a click. **Open its page in the documentation** opens the control's page in your browser. With touch or a pen, a tap opens the card.
- With the keyboard, ((Tab)) to a name in bold or code and press ((Enter)).

When the control is not on screen at the moment, the app shows what brings it out:

- A control that **Advanced** hides is found with **Search the settings**: its name goes into the field. The next jump puts back what you had typed there.
- A control that shows only in some cases points at what decides it. **Save as**, for example, shows only while **Match the source** is off, so **Match the source** is ringed.
- A control of another source, such as a video setting while **Live** is on, rings the buttons in the top bar that switch to it.
- When there is nothing to show, the control's page of the documentation opens instead.

A key, a click or the mouse wheel fades the ring at once.

## It works online {#online}

AI Q&A runs on the servers of [Mintlify](https://mintlify.com), a documentation service, not on your computer. So it needs an internet connection. When the panel cannot load, it says **AI Q&A could not be loaded** and offers **Try again** and **Open documentation**.

It can also pause for a while. Mintlify gives the project a monthly allowance, and everyone who uses the app shares it. When the allowance runs out, AI Q&A stops answering until the next month.

So please ask where it helps most: when you don't know which page to read. To look up one control, [All settings](settings.html) or the search at the top of these pages is often quicker. This documentation is always there, with or without AI Q&A.

## When a question gets no answer {#notices}

When a question cannot be answered, a notice appears above the box you type in. Its buttons offer the way on, and **Open documentation** opens these pages in your browser.

| The notice says | What to do |
|---|---|
| AI Q&A is unavailable right now. {#notice-off} | This month's allowance may be used up, or the service is down. Look it up in the documentation, or click **Try again** later. |
| Too many questions in a short time. {#notice-busy} | Wait a moment, then click **Try again**. |
| Your question could not be sent. {#notice-net} | Check your connection, then click **Try again**. |
| This conversation has reached its length limit. {#notice-full} | Click **New conversation**, then ask again. |

((Esc)) or the notice's **×** closes the notice. The next ((Esc)) closes the panel.

## What is sent, and what stays {#privacy}

- **Nothing starts before you open it.** The app starts the panel at your first click on **Ask AI**. Until then, nothing is sent.
- **What you type goes to Mintlify**, which writes the answer. Leave out anything private, such as an MCP key or a folder path with your name in it. Mintlify also receives usage events from the panel, and checks questions with hCaptcha to keep bots out.
- **Your files stay on your computer.** The app never sends your pictures, videos, settings or library. If the panel offers to attach a file, whatever you attach or paste there goes to Mintlify too.
- **The panel keeps its cookies and storage** in `%LOCALAPPDATA%\VRChatDLSS5Cam\webview2`.
- **The app's log** notes the HTTP status and the length of each answer. Your questions and the answers' text are never written to it.

All the details: [Privacy and licences](privacy.html#app-ai-qa).

## Without WebView2, and on Linux {#browser}

The panel needs the **Microsoft Edge WebView2 Runtime**, a Microsoft component that shows web pages inside apps. Windows 11 has it, and so do most Windows 10 computers.

<!-- if askWidget -->
Where it is missing or cannot start, and in the [Linux package](linux.html), **Ask AI** opens the AI Q&A of this documentation in your browser instead. The app says **AI Q&A opened in your browser**. The answers come from the same pages.
<!-- else -->
Where it is missing or cannot start, and in the [Linux package](linux.html), **Ask AI** opens this documentation in your browser instead.
<!-- endif -->

To have the panel inside the app, install the WebView2 Runtime from [Microsoft's download page](https://developer.microsoft.com/microsoft-edge/webview2/), then start the app again.
