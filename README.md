# Lua with Macros

This is Lua 5.3 with the simplest macro form -- text replacement. Coming soon
are basic macro functions and then reader macros for manipulating the parsing of
Lua's syntax.

## Simple Macro Form

The macro form that is currently supported is `macro <name> <string>` where
`<name>` is the string in the code that is replaced by `<string>`. The string
has to be a string in Lua, meaning the replacement needs to be wrapped by
double- quotes or the long-string designators `[[` and `]]`. I prefer the 
long-string form because it keeps one from having to escape all sorts of code.
Here is a very simple example:

    macro foo [[bar]]
    print(foo)

Which yields `bar` to the terminal. Another, more fun, example:

    macro DEFINE [[macro]]
    DEFINE foo [[print]]
    DEFINE bar [[("bar")]]
    foobar

Which again yields `bar` to the terminal.

## Testing

This simple macro system has not been tested very extensively and needs some
sort of fuzzer so the system can be more robust against incorrect forms.
