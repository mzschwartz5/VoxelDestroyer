
RWBuffer<float> beforePaintValues : register(u0);
RWBuffer<float> afterPaintValues : register(u1);

cbuffer PaintDeltaCB : register(b0)
{
    int numElements;
    int sign; // +1 redo, -1 undo (and -1 for computing delta)
    int padding0;
    int padding1;
};

// Computes the delta between two sets of paint values and writes it in place to the before buffer.
// This compute step can also (un-)apply a delta by using the "sign" uniform and storing the delta in the "before" buffer.
[numthreads(VGS_THREADS, 1, 1)]
void main(uint3 gId : SV_DispatchThreadID)
{
    uint index = gId.x;
    if (index >= numElements) return;

    float beforeValue = beforePaintValues[index];
    float afterValue = afterPaintValues[index];

    float delta = afterValue + sign * beforeValue;
    beforePaintValues[index] = delta;
}