# glottony

Museum of polyglot gluttony, of fun language interpreters.

## cortex

TeX macros. Conditionals, definitions, arithmetic, e-TeX extensions. No typesetting.

```sh
$ echo '\def\twice#1{#1#1}\twice{abc}\end' | ./cortex.c
abcabc
```

## joy

Joy. Stack-based, purely functional, concatenative.

```sh
$ echo '5 dup * .' | ./joy.c
25
```

## mint

MINT (from Freemacs). `#(func,args)` string processing.

```sh
$ echo '#(ds,greet,Hello #1!)#(greet,World)' | ./mint.c
Hello World!
```

## strac

TRAC primitives with `[func,args]` syntax.

```sh
$ echo '[ds,name,Alice][cl,name]' | ./strac.c
Alice
```

## Design

- Literate — docs in file headers
- Single-file — ccraft shebang, no build system
- Faithful — follows original semantics

## Testing

Tests live alongside each source (`.t` suffix). Filter by name: `./cortex.t /ifnum`.
