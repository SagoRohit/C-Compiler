int g[5];

int main(){
	int i;
	int local[5];

	for(i = 0; i < 5; i++){
		g[i] = i * 10;
		local[i] = i * i;
	}

	for(i = 0; i < 5; i++){
		println(g[i]);
	}

	for(i = 0; i < 5; i++){
		println(local[i]);
	}

	g[2]++;
	println(g[2]);

	return 0;
}
