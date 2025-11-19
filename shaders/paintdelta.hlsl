
RWBuffer<float> beforePaintValues : register(u0);
RWBuffer<float> afterPaintValues : register(u1);

cbuffer PaintDeltaCB : register(b0)
{
    int numElements;
    int sign; // +1 redo, -1 undo (and -1 for computing delta)
    int padding0;
    int padding1;
};

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