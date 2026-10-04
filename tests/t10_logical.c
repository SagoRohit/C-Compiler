int main(){
	int a, b;

	a = 1;
	b = 0;

	println(a && b);
	println(a || b);
	println(!a);
	println(!b);

	if((a > 0 && b < 5) || (a < 0 && b > 5)){
		println(1);
	}
	else{
		println(0);
	}

	return 0;
}
