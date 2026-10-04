int main(){
    int a, b, c, d;
    a = 5;
    b = -a;
    println(b);
    b = +a;
    println(b);
    c = !a;
    println(c);
    d = 0;
    c = !d;
    println(c);
    b = a++;
    println(b);
    println(a);
    b = a--;
    println(b);
    println(a);
    return 0;
}
