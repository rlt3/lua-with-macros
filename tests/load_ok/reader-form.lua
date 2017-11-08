readermacro foo (nextc, curr, nextw)
    return [[bar]]
end
assert("foo()" == "bar()", [[Base form for reader macros.]])

readermacro first (nextc, curr)
    return curr()
end
assert("\first" == "\"", [[Reader's curr is first char after macro name.]])

readermacro skip2 (nextc)
    nextc(); nextc();
    return [["bar"]]
end
assert(skip2-- == skip2(), [[`nextc` consumes next characters in buffer.]])
 
readermacro foreach (nextc, curr, nextw)
    -- for i, `v' = ipairs(`t') do
    local v = nextw()
    assert(nextw() == "as", "Missing `as' in 'foreach'")
    local t = nextw()
    assert(nextw() == "do", "Missing `do' in 'foreach'")
    return string.format([[for i, %s in ipairs(%s) do]], t, v)
end

local nums = { 1, 2, 3 }
foreach nums as n do
    assert(n == i, [[`nextw` consumes whitespace bound, 
                    like-characters and returns them as strings.]])
end

--readermacro >> (nextc, curr, nextw)
--    local code = ""
--    local word = ""
--    while word ~= "<<" do
--        word = nextw()
--        code = code .. word
--        if (curr() == " ") then
--            code = code .. " "
--        end
--    end
--    return string.format([[ [==[%s]==] ]], code:sub(1, -4))
--end
--
--assert("for i = 1 #t do" == >>for i = 1 #t do<< )
--
--readermacro | (nextc, curr, nextw)
--    local code = ""
--    local word = nextw("|")
--    repeat
--        code = code .. word
--        if (curr() == " ") then
--            code = code .. " "
--        end
--        word = nextw("|")
--    until word == "|"
--    return string.format([[ [==[%s]==] ]], code)
--end
--
--assert("for i = 1 #t do" == |for i = 1 #t do|)
--assert("for i = 1 #t do" == |for i = 1 #t do| )
