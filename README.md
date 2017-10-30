# Lua with Macros

This is Lua 5.3 with two macro forms: simple code replacement and function
macros. Coming soon are cool reader macros.

Also coming soon is a way to represent strings-as-code in a different
way than actual strings not unlike `backqoute` and `quote` in Common Lisp.

## Simple Code Replacement Form

This macro form is simple code replacement. Its form is `macro <name> <string>`
where `<name>` is the code that is replaced by `<string>`. The string has to be
a string in Lua, meaning the replacement needs to be wrapped by double-quotes
or the long-string designators `[[` and `]]`. I prefer the long-string form
because it keeps one from having to escape all sorts of code.  Here is a very
simple example:

    macro foo [["bar"]]
    assert("bar" == foo)
    print(foo)

Which yields `bar` to the terminal. Another, more fun, example:

    macro DEFINE [[macro]]
    DEFINE foo [[print]]
    DEFINE bar [[("bar")]]
    foobar

Which again yields `bar` to the terminal.

## Function Macro Form

The function macro form is `macro <name> ([<arg>|,]+) [<expression>]+ end`. The
`<name>` is simply the name of the function macro that is inevitably replaced
by the return value of the function. `<arg>`s can contain anything except
commas (`,`) right now (if you want it to work right).  Here's an example of a
function macro to keep from typing inserts:

    macro insert (t, v)
        -- t[#t+1] = v
        return string.format([[%s[#%s+1] = %s]], t, t, v)
    end

    local t = {}
    insert(t, 1); insert(t, 2)
    insert(t, 3); insert(t, 4)
    insert(t, 5)
    for i = 1, #t do
        print(t[i])
    end

Here's a simple example of someone hating that they're typing an `ipairs` loop
over and over again:

    macro foreach (t, var)
        -- for i, var in ipairs(t) do
        return string.format([[for i, %s in ipairs(%s) do]], var, t)
    end

    local nums = { 6, 7, 8, 9, 10 }
    foreach(nums, n)
        print(n)
    end

And the previous `foreach` supports inline like any regular Lua code would:

    foreach(nums, n) print(n) end

Macro functions are regular Lua functions, so features like local variables,
closures, errors, and anonymous functions are as you'd expect:

    macro insert (t, v)
        function checker ()
            if (not t) then error("t can't be nil in insert macro") end
            v = v or [["default value"]]
        end
        local code = function ()
            return string.format([[%s[#%s+1] = %s]], t, t, v)
        end
        checker()
        return code()
    end

    local t = {}
    insert(t, 1)
    insert(t, 2)
    insert(t)
    insert() -- throws error
    for i = 1, #t do
        print(t[i])
    end

If the erroneous macro wasn't there (comment it out), this would print the
following to the terminal:

    1
    2
    default value

## Debugging

I plan to add a fancy `macroexpand` later but for now one can simply call
`print("<macro>")` and get a string representation of the expanded macro form.

## Testing

The testing system I've added is for three things (in this order):
preventing memory leaks, regression testing, and documenting minute 
functionality.

Because of importance of preventing memory leaks, especially when parsing 
incorrect or broken code, the testing suite has been setup with two folders: 
`load_ok` and `load_err`. If you have a `.lua` test file which should error
then it should be put into the `load_err` directory. Otherwise it should be
put into the `load_ok` directory. If any file in any of these directories
reports errors to valgrind or fails an assert then the build should be
considered failing.

If you can think of any tricky condition that would really put the system to
the test I would really appreciate a pull request or an email.

## Issues

This simple macro system has not been tested extensively and needs some sort of
fuzzer so the system can be more robust against incorrect forms.

Line numbers also aren't working correctly so error reporting is a tad
confusing right now.
