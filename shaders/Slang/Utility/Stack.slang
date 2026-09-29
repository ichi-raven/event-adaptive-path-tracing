module Stack;

public struct Stack<typename Type, let capability : uint32_t>
{
    public __init()
    {
        pos = 0;
    }

    [mutating]
    public void push(const Type val)
    {
        data[pos] = val;
        pos       = min(pos + 1, int(capability));
    }

    [mutating]
    public Type pop()
    {
        pos = max(0, pos - 1);
        return data[pos];
    }

    [mutating]
    public void clear()
    {
        pos = 0;
    }

    public Type top()
    {
        return data[pos - 1];
    }

    public int32_t size()
    {
        return pos;
    }

    public bool flood()
    {
        return pos >= int(capability);
    }

    public bool empty()
    {
        return pos == 0;
    }

    Type data[capability];
    int32_t pos;
}
