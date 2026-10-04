int add(int a, int b){
	return a + b;
}

int factorial(int n){
	if(n <= 1){
		return 1;
	}
	return n * factorial(n - 1);
}

void greet(){
	println(999);
}

int main(){
	int s, f;

	s = add(3, 4);
	println(s);

	f = factorial(5);
	println(f);

	greet();

	println(add(1, add(2, 3)));

	return 0;
}
