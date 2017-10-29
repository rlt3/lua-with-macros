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
insert()
