# whisper.cpp for ScribeAR

See [the original repo](https://github.com/ggerganov/whisper.cpp) for README of whisper.cpp

## A Primer on WASM

### What is WASM?

To paraphrase [wikipedia](https://en.wikipedia.org/wiki/WebAssembly), WebAssembly (WASM) was created to let us to run code at "near-native" speed on the front-end. 

WASM achieves this by creating a binary-format, low-level, compiled language that can be directly executed by a browser. Developers would code in a high level language, i.e. C, then use a special compiler to compile their code to WASM code, which can then be served to the frontend and ran. (Constrast this with javascript, which is sent as plain text to the browser and *interpreted*)

Since WASM is an open standard, many compiler toolchains exist. For ScribeAR we chose [emscripten](https://emscripten.org/docs/introducing_emscripten/about_emscripten.html),  a gcc-like C / C++ to WASM compiler - mainly because whisper.cpp chose that.

### How does Emscripten work?

Refer to the [MDN](https://developer.mozilla.org/en-US/docs/WebAssembly/Concepts) and [Emscripten](https://emscripten.org/docs/compiling/index.html) official documentations for more juicy info. We recommend going through this [tutorial](https://developer.mozilla.org/en-US/docs/WebAssembly/C_to_Wasm) first to get a feeling of it running.

Similar to gcc, emscripten takes in a bunch of `.c` or `.cpp` files, and compiles them into a single executable `.wasm` file. However, since the `.wasm` file must be able to intereact with a webpage (and the browser at large), it also generates a 'glue' `.js` file that loads and supports the WASM code. Optionally, it can also generate a demo `.html` file that runs the WASM code, but we will soon see how to run the WASM code in our own webpage.

<img src='result.png' alt='A typical compilation result'>

Also similar to gcc, what exactly emscripten outputs can be controlled with the `-o` flag.

The demo `hello.html` file just runs the WASM code and print out the output. How does it do that? If you dig into it, you should see two `<script>` elements. The first sets up a *global* object called `Module` with some members, and the second one just includes the glue `hello.js` file.

<img src='Module.png'>

This `Module` object serves as an interface between `hello.html` (and our js code in general) and the WASM code. Recall that when an `html` file is loaded, the (non-module non-async) scripts are ran in order. Thus, the first `<script>` runs, initializes the `Module` objects, and uses its members to pass values and callbacks to `hello.js`. Then the second `<script>` runs `hello.js`, which loads and runs the WASM code (specifically its `main` function) using the arguments in `Module`. Thus, `hello.html` can pass data to WASM, and WASM can pass data back.

(More specifically, the `print` member of `Module` serves as a `stdout` redirect, so to speak. It is called whenever the WASM code tries to print to `stdout`. See [here](https://emscripten.org/docs/api_reference/module.html) for a full specification of `Module`)

You may realize that there are two major problems with how WASM is ran so far:

1. It relies on `<script>` tags executed in order, which doesn't work once we move from plain `html` files to something like React
2. There is no way to directly call a C function in our JS code, or vice versa (`print` is called implicitly when we `printf` in C)

There is also a more hidden third problem - What happens when we step it up and introduce pthreads to our C program? 

We will see how all of these can be solved in the following sections on `modularize`, binding stuff, and web workers.

### WASM code but as a Module

As we just said, relying on `<script>` tags limit what we can do with WASM quite a lot. Luckily, there is an emscripten option aptly named `modularize` that outputs `hello.js` as a JS module exporting a *constructor* for `Module`, which can be ran anywhere at any time.

(This is also a good place to introduce the myriad of options emscripten has, which are helpfully listed on this [very hidden website](https://emsettings.surma.technology). To enable an option add `-s OPTION` to the `emcc` command)



## Building WASM whisper for ScribeAR