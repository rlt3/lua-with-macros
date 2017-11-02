readermacro foo (next, curr)
    return [[bar]]
end
assert("foo()" == "bar()", [[Base form for reader macros.]])

readermacro first (next, curr)
    return curr
end
assert("first" == "t", [[Reader's curr is always last char of macro name.]])

readermacro skip2 (next, curr)
    next(); next();
    return [["bar"]]
end
assert(skip2-- == skip2(),
        [[Next without arguments consumes next character from input buffer]])
 
readermacro foreach (next, curr)
    -- for i, `v' = ipairs(`t') do
    local v = next(true)
    assert(next(true) == "as", "Missing `as' in 'foreach'")
    local t = next(true)
    assert(next(true) == "do", "Missing `do' in 'foreach'")
    return string.format([[for i, %s in ipairs(%s) do]], t, v)
end

local nums = { 1, 2, 3 }
foreach nums as n do
    assert(n == i,
        [[Calling next with true parses white-space bounded strings.]])
end
