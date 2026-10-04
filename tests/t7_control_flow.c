int main(){
	int i;

	i = 10;
	if(i > 5){
		println(i);
	}

	if(i < 5){
		println(1);
	}
	else{
		println(0);
	}

	for(i = 0; i < 5; i++){
		println(i);
	}

	i = 3;
	while(i > 0){
		println(i);
		i--;
	}

	return 0;
}
