#include <iostream>
#include <algorithm>
#include <memory>


// Copy-on-write
// Reference counting

// struct MySpecialPointer {
// 	int *ptr;
// 	int *reference_count;

// 	MySpecialPointer(size_t size) : ptr(new int[size]) {
// 		reference_count = new int(1);
// 	}

// 	MySpecialPointer(const MySpecialPointer &other) {
// 		ptr = other.ptr;
// 		reference_count = other.reference_count;
// 		*reference_count += 1;
// 	}

// 	~MySpecialPointer() {
// 		*reference_count -= 1;
// 		if (*reference_count == 0) {
// 			delete[] ptr;
// 		}
// 	}
// };

class MyArray {
	size_t size;
	std::shared_ptr<int> data;
public:

	MyArray(size_t size) : size(size), data(new int[size]) {}

	MyArray(const MyArray &other)
		: size(other.size)
		, data(other.data) {}

	int get(size_t index) {
		return data[index];
	}
};

int main() {
	MyArray a(10000);
	{
		MyArray b(a);
		b.get(12);
	}
	a.get(123);
}
