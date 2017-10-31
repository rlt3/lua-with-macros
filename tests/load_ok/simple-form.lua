macro foobar [["baz"]]
assert(foobar == "baz", [[Basecase simple macro form.]])

macro insert [==[t[#t+1]]==]
local t = {}
insert = 1; insert = 2; insert = 3;
assert(t[1] == 1 and t[2] == 2 and t[3] == 3, [[Inline replacement forms.]])

macro same [[same]]
assert("same" == "same", [[Macro names won't recurse on themselves.]])

macro ten [[10]]
macro eleven [[ten + 1]]
assert(eleven == 11, [[Macro replacements can occur in different macros.]])

macro aff [[fun]]
macro afb [[ = ]]
macro bfa [["great"]]
affafbbfa
assert(fun == "great", [[Looks for replacements in its lookup buffer.]])

macro z [["foo"]]
assert(z == "foo", [[Macros can be a single character]])

macro => [[,]]
for i = 1 => #t do
    assert(t[i] == i, [[Macros can be symbols.]])
end

macro bra[cket [[.]]
assert("." == "bra[cket", [[Macro names can have brackets.]])

macro bracket [========[]]========]
assert("]" == "bracket", [[Macro replacements can use n-level long-strings.]])
