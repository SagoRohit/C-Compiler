int a, b;
int main()
{
    int x, y;

    a = 10;

    b = 20;

    x = 100;

    y = 200;

    println(a);

    goto L1;
L2:

    println(b);

    goto L3;
L1:

    println(x);

    goto L2;
L3:

    println(y);

    return 0;
}