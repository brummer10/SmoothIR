/*
 * HybridConvolverMT.h
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *
 * Features:
 *  - Zero latency FIR head
 *  - Async FFT tail worker
 *  - Triple buffered tail exchange
 *  - In-place safe
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#pragma once

#include <vector>
#include <complex>
#include <atomic>
#include <thread>
#include <cstring>
#include <fftw3.h>

class HybridConvolverMT {
public:
    using Complex = std::complex<double>;

    HybridConvolverMT(size_t blockSize = 256, size_t headSize  = 64) :
         B(blockSize), H(headSize), N(blockSize * 2) {
        allocateWorker();
    }

    ~HybridConvolverMT() {
        running = false;
        workerCV.notify_all();
        pendingJobs.fetch_add(1, std::memory_order_release);
        if (worker.joinable()) worker.join();
        destroyFFT();
    }

    bool isActivated() const {
        return isInited;
    }

    size_t getNumParts() const {
        return numParts;
    }

    void setIR(const std::vector<double>& ir) {
        running = false;
        workerCV.notify_all();
        if (worker.joinable()) worker.join();
        const size_t irLen = ir.size();

        head.assign(H, 0.0);
        for (size_t i = 0; i < H && i < irLen; ++i)
            head[i] = ir[i];

        const size_t tailLen = (irLen > H) ? (irLen - H) : 0;

        numParts = (tailLen + B - 1) / B;
        H_parts.clear();
        H_parts.resize(numParts);
        X_history.clear();
        X_history.resize(numParts, std::vector<Complex>(N));

        for (auto& h : X_history) h.resize(N);
        for (auto& h : H_parts) h.resize(N);

        for (size_t p = 0; p < numParts; ++p) {
            H_parts[p].assign(N, Complex(0.0));
            X_history[p].assign(N, Complex(0.0));
        }

        for (size_t p = 0; p < numParts; ++p) {
            std::fill(worker_fft_in.begin(), worker_fft_in.end(), Complex(0.0));
            for (size_t i = 0; i < B; ++i) {
                size_t idx = H + p * B + i;
                if (idx < irLen)
                    worker_fft_in[i] = ir[idx];
            }

            fftw_execute(worker_plan_fwd);
            H_parts[p] = worker_fft_out;
        }

        delayLine.assign(H, 0.0);
        for (auto& b : tailBuffers)
            b.assign(B, 0.0);

        overlap.assign(B, 0.0);
        writeTailIndex.store(0);
        readTailIndex.store(0);
        isInited = true;
        running = true;
        worker = std::thread(&HybridConvolverMT::workerLoop, this);
    }

    void process(const float* input, float* output) {
        if (!isInited) return;

        for (size_t i = 0; i < B; ++i) {
            double acc = 0.0;

            for (size_t k = 0; k < H; ++k) {
                int idx = (int)i - (int)k;
                double x = (idx >= 0) ? input[idx] : delayLine[H + idx];
                acc += x * head[k];
            }
            output[i] = (float)acc;
        }

        const int r = readTailIndex.load(std::memory_order_acquire);
        const auto& tail = tailBuffers[r];

        for (size_t i = 0; i < B; ++i)
            output[i] += tail[i];

        for (size_t i = 0; i < H; ++i)
            delayLine[i] = input[B - H + i];

        int w = inputWrite.load(std::memory_order_relaxed);
        std::memcpy(inputBuffers[w].data(), input, sizeof(float) * B);
        inputRead.store(w, std::memory_order_release);
        inputWrite.store(1 - w, std::memory_order_release);
        pendingJobs.fetch_add(1, std::memory_order_release);
        workerCV.notify_one();
    }

private:
    size_t B = 0;
    size_t H = 0;
    size_t N = 0;
    size_t numParts = 0;
    bool isInited = false;
    std::vector<double> head;
    std::vector<double> delayLine;
    std::vector<std::vector<Complex>> H_parts;
    std::vector<std::vector<Complex>> X_history;
    std::vector<float> tailBuffers[3];
    std::atomic<int> readTailIndex{0};
    std::atomic<int> writeTailIndex{1};
    std::vector<float> inputBuffers[2];
    std::atomic<int> inputWrite{0};
    std::atomic<int> inputRead{1};
    std::vector<double> overlap;
    std::vector<Complex> worker_fft_in;
    std::vector<Complex> worker_fft_out;
    std::thread worker;
    std::atomic<bool> running{false};
    std::mutex workerMutex;
    std::condition_variable workerCV;
    std::atomic<uint64_t> pendingJobs{0};
    fftw_plan worker_plan_fwd = nullptr;
    fftw_plan worker_plan_inv = nullptr;

    void workerLoop() {
        while (true) {
            std::unique_lock<std::mutex> lock(workerMutex);
            workerCV.wait(lock, [&]{
                return !running || pendingJobs.load(std::memory_order_acquire) > 0;
            });

            if (!running) break;
            pendingJobs.fetch_sub(1, std::memory_order_release);
            lock.unlock();
            processTail();
        }
    }

    void processTail() {
        if (numParts == 0) return;

        std::fill(worker_fft_in.begin(), worker_fft_in.end(), Complex(0.0));
        const int r = inputRead.load(std::memory_order_acquire);
        const auto& in = inputBuffers[r];

        for (size_t i = 0; i < B; ++i)
            worker_fft_in[i] = in[i];

        fftw_execute(worker_plan_fwd);

        if (numParts > 1) {
            for (size_t i = numParts - 1; i > 0; --i) {
                std::memcpy(X_history[i].data(), X_history[i - 1].data(), sizeof(Complex) * N);
            }
        }

        std::copy(worker_fft_out.begin(), worker_fft_out.end(), X_history[0].begin());
        std::fill(worker_fft_in.begin(), worker_fft_in.end(), Complex(0.0));

        for (size_t p = 0; p < numParts; ++p) {
            const auto& X = X_history[p];
            const auto& H = H_parts[p];

            for (size_t i = 0; i < N; ++i)
                worker_fft_in[i] += X[i] * H[i];
        }

        fftw_execute(worker_plan_inv);

        const int currentRead = readTailIndex.load(std::memory_order_acquire);
        int nextWrite = (currentRead + 1) % 3;
        auto& tail = tailBuffers[nextWrite];
        for (size_t i = 0; i < B; ++i) {
            tail[i] = (float)( worker_fft_out[i].real() / N + overlap[i]);
        }

        for (size_t i = 0; i < B; ++i) {
            overlap[i] = worker_fft_out[i + B].real() / N;
        }

        readTailIndex.store(nextWrite, std::memory_order_release);
    }

    void allocateWorker() {
        inputBuffers[0].assign(B, 0.0f);
        inputBuffers[1].assign(B, 0.0f);
        worker_fft_in.assign(N, Complex(0.0));
        worker_fft_out.assign(N, Complex(0.0));

        for (auto& b : tailBuffers)
            b.assign(B, 0.0f);

        worker_plan_fwd = fftw_plan_dft_1d((int)N, reinterpret_cast<fftw_complex*>(
                worker_fft_in.data()), reinterpret_cast<fftw_complex*>(worker_fft_out.data()),
                FFTW_FORWARD, FFTW_MEASURE);

        worker_plan_inv = fftw_plan_dft_1d((int)N, reinterpret_cast<fftw_complex*>(
                worker_fft_in.data()), reinterpret_cast<fftw_complex*>( worker_fft_out.data()),
                FFTW_BACKWARD, FFTW_MEASURE);
    }

    void destroyFFT() {
        if (worker_plan_fwd)
            fftw_destroy_plan(worker_plan_fwd);

        if (worker_plan_inv)
            fftw_destroy_plan(worker_plan_inv);
    }
};
