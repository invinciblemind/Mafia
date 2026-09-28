#include <iostream>
using namespace std;

int main() {
    int N, k = 3;
    cin >> N;
    cout << static_cast<double>(N) / k << ' ' << floor(static_cast<double>(N) / k) << ' ' << N / k << endl;
    cout << __cplusplus << endl;
    return 0;
}
