macro insert (t, v)
    local code = function ()
        return string.format([[%s[#%s+1] = %s]], t, t, v)
    end
    function checker ()
        if (not t) then error("t can't be nil in insert macro") end
        v = v or [["default value"]]
    end
    checker()
    return code()
end
t = {}
insert(t, 1)
assert(t[1] == 1, [[Basecase function form.]])


insert(t, 2); insert(t, 3); insert(t, 4);
assert(t[2] == 2 and t[3] == 3 and t[4] == 4, [[Inline function form.]])


insert(t)
assert(t[5] == "default value", [[Arguments work like Lua and can be nil.]])


macro put (c)
    return string.format("%s", c)
end
assert("put()" == "nil", [[Macros can be passed zero arguments.]])


macro comma [[,]]
assert("nil" == "put(comma)", [[Commas aren't recognized in function lists.]])


macro separator ()
    return [[,]]
end
assert("separator()" == ",", [[Macro functions can have zero parameters.]])
assert("separator(foo, bar)" == ",", [[Macro functions can throwaway args.]])


macro foreach (t, var)
    -- for i, var in ipairs(t) do
    return string.format([[for i, %s in ipairs(%s) do]], var, t)
end
t[5] = 5
foreach(t separator(put(foo)) n)
    assert(n == i, [[Macro functions can have macros in argument list.]])
end


macro set (c)
    return put(c)
end
assert("foo" == "set(foo)", [[Macro functions can be contained in one another.]])
assert("bar" == "set(bar)", [[Contained macros don't keep state pt1.]])
assert("baz" ~= "set(jaz)", [[Contained macros don't keep state pt2.]])


macro sideeffect (c)
    globalvar = c
    return put(c)
end
assert("foo" == "sideeffect(foo)" and globalvar == "foo",
       [[Macro functions have access to the global state.]])


macro z (c)
    return put(c)
end
assert("foo" == "z(foo)", [[Macro functions can be a single character.]])


macro => ()
    return [["foobar"]]
end
assert(=>() == "foobar", [[Macro function names can be symbols.]])
