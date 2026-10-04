int x;

int main(){
    int y;
    x = 100;
    y = 5;
    {
        int y;
        y = 9;
        println(y);
    }
    println(y);
    println(x);
    return 0;
}
