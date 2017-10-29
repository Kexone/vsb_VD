﻿#include "exercise3.h"
#include <iostream>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <random>
#include "utils.h"

float gaussian(float x, float y, float A = 1.0f, float x0 = 0, float y0 = 0, float spreadX = 1.5f, float spreadY = 1.5f) {
	return A * std::exp(-(
		(Utils::sqr<float>(x - x0) / (2 * Utils::sqr<float>(spreadX))) +
		(Utils::sqr<float>(y - y0) / (2 * Utils::sqr<float>(spreadY)))
		));
}

void Exercise3::run()
{
	cv::Size standardSize(800, 640);
	cv::Mat sample_points(N, gd, CV_32F);
	cv::Mat sample_values(N, 1, CV_32F);

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_real_distribution<float> disX(x0, x1);
	std::uniform_real_distribution<float> disY(y0, y1);

	for( int i = 0; i < N; i++)
	{
		float x = disX(gen);
		float y = disY(gen);

		sample_points.at<cv::Vec2f>(i,0) = cv::Vec2f(x, y);
		sample_values.at<float>(i) = gaussian(x, y);
	}

	cv::flann::KDTreeIndexParams index;
	cv::flann::Index nn(sample_points, index);
	cv::flann::SearchParams search_params(256);

	cv::Mat query(1, gd, CV_32F);
	cv::Mat indices(N, 1, CV_32S);
	cv::Mat dist(N, 1, CV_32F);
	cv::Mat recons(standardSize, CV_32FC3);
	cv::Mat nearest(standardSize, CV_32FC3);

	for (int x = 0; x < recons.rows; x++)
	{
		for (int y = 0; y < recons.cols; y++)
		{
			float yn = (y / (float)recons.cols) * (y1 - y0) + y0;
			float xn = (x / (float)recons.rows) * (x1 - x0) + x0;
			query.at<float>(0) = yn;
			query.at<float>(1) = xn;

			const int noIndices = nn.radiusSearch(query, indices, dist, radius, N, search_params);
			float W = 0, WP = 0;

			for (int i = 0; i < noIndices; i++)
			{
				float v = sample_points.at<float>(indices.at<int>(i));
				float d = dist.at<float>(i);
				auto w = Utils::sqr<float>(static_cast<float>(std::fmax(0, radius - d)) / (radius*d));
				WP += w * v;
				W += w;
			}

			nearest.at <cv::Vec3f>(x, y) = cv::Vec3f(0, sample_values.at<float>(indices.at<int>(0)), 1.f);
			recons.at<cv::Vec3f>(x, y) = cv::Vec3f(0, WP / W, 1.f);
		}
	}


	cv::imshow("Flann", recons);
	cv::imshow("Nearest neighbour", nearest);
	cv::waitKey(0);
}