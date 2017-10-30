macro foo ()
    local bar = function ()
        return "baz"
    end
    return bar
end
print(foo())
