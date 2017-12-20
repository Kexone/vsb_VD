﻿#include "stdafx.h"

using std::mt19937;
using std::uniform_real_distribution;

typedef mt19937                                     Engine;
typedef uniform_real_distribution<float>            Distribution;

auto uniform_generator = std::bind( Distribution( 0.0f, 1.0f ), Engine( 1 ) );

float Random( const float range_min, const float range_max )
{
	int ksi;

#pragma omp critical ( random ) 
	{
		ksi = rand();

		//ksi = uniform_generator();
	}

	return ( static_cast< float >( ksi ) / ( RAND_MAX + 1 ) ) *
		( range_max - range_min ) + range_min;
}

CellIndices::CellIndices( const int i, const int j, const int k )
{
	this->i = i;
	this->j = j;
	this->k = k;
}

Volume::Volume( const int width, const int height, const int n, const Vector3 & cell_size )
{
	assert( ( width > 1 ) && ( height > 1 ) && ( n > 1 ) );
	assert( CACHE_LINE_SIZE % kElement_size == 0 );
	assert( ( cell_size.x > 0 ) && ( cell_size.y > 0 ) && ( cell_size.z > 0 ) );

	width_ = width;
	height_ = height;
	n_ = n;

	{
		const int pad = ( ( kElement_size * width_ ) % CACHE_LINE_SIZE );
		width_step_ = ( ( kElement_size * width_ ) + pad ) / kElement_size;
	}

	data_ = new DATA_TYPE[width_step_ * height_ * n_];
	printf( "Volume data size: %0.1f MB\n", ( kElement_size * width_step_ * height_ * n_ ) / SQR( 1024.0 ) );

	cell_size_ = cell_size;
	half_volume_size_ = Vector3( cell_size_.x * ( n_ - 1 ), cell_size_.y * ( width_ - 1 ), cell_size_.z * ( height_ - 1 ) ) / 2;

	const Vector3 A = cell( 0, 0, 0 ).A();
	bounds_ = AABB( A, -A );
}

Volume::~Volume()
{
	SAFE_DELETE_ARRAY( data_ );
}

void Volume::Load( std::string & file_name_mask, const int first_slice_index, const int last_slice_index )
{
	printf( "Loading data...\n" );

	for ( int i = 0, n = 1; i < last_slice_index - first_slice_index + 1; ++i, ++n )
	{
		char file_name[128] = { 0 };
		sprintf( file_name, file_name_mask.c_str(), i + first_slice_index );

		cv::Mat slide_8u = cv::imread( std::string( file_name ), cv::IMREAD_GRAYSCALE );

		assert( slide_8u.cols == width_ );
		assert( slide_8u.rows == height_ );
		assert( n <= n_ );

		for ( int j = 0; j < slide_8u.cols; ++j )
		{
			for ( int k = 0; k < slide_8u.rows; ++k )
			{
				data_[offset( i, j, k )] = static_cast< DATA_TYPE >( slide_8u.at<uchar>( slide_8u.rows - 1 - k, j ) / 255.0 );
			}
		}

		printf( "\r%0.1f %%", ( 100.0 * n ) / n_ );
	}

	printf( "\rDone.   \n" );
}

Cell Volume::cell( const CellIndices & indices ) const
{
	return cell( indices.i, indices.j, indices.k );
}

// [i, j, k] je index buňky
Cell Volume::cell( const int i, const int j, const int k ) const
{
	assert( ( i >= 0 ) && ( i < n_ - 1 ) );
	assert( ( j >= 0 ) && ( j < width_ - 1 ) );
	assert( ( k >= 0 ) && ( k < height_ - 1 ) );

	DATA_TYPE rhos[8];

	DATA_TYPE & rho_A = rhos[0];
	DATA_TYPE & rho_B = rhos[1];
	DATA_TYPE & rho_C = rhos[2];
	DATA_TYPE & rho_D = rhos[3];

	DATA_TYPE & rho_E = rhos[4];
	DATA_TYPE & rho_F = rhos[5];
	DATA_TYPE & rho_G = rhos[6];
	DATA_TYPE & rho_H = rhos[7];

	rho_A = data_[offset( i, j, k )];
	rho_B = data_[offset( i + 1, j, k )]; // B je A pro buňku [i + 1, j, k], atd.
	rho_C = data_[offset( i + 1, j + 1, k )];
	rho_D = data_[offset( i, j + 1, k )];

	rho_E = data_[offset( i, j, k + 1 )];
	rho_F = data_[offset( i + 1, j, k + 1 )];
	rho_G = data_[offset( i + 1, j + 1, k + 1 )];
	rho_H = data_[offset( i, j + 1, k + 1 )];

	const Vector3 A = Vector3( cell_size_.x * i, cell_size_.y * j, cell_size_.z * k ) - half_volume_size_;

	return Cell( rhos, A, A + cell_size_ );
}

CellIndices Volume::cell_indices( const Vector3 & p ) const
{
	const Vector3 tmp = ( p - bounds_.lower_bound() ) / cell_size_;

	return CellIndices(
		MAX( 0, MIN( n_ - 2, static_cast< int >( floor( tmp.x ) ) ) ), // indexy první zasažené buňky
		MAX( 0, MIN( width_ - 2, static_cast< int >( floor( tmp.y ) ) ) ),
		MAX( 0, MIN( height_ - 2, static_cast< int >( floor( tmp.z ) ) ) ) );
}

void Volume::Traverse( Ray & ray, std::vector<CellHit> & traversed_cells )
{
	float t0 = 0;
	float t1 = REAL_MAX;

	traversed_cells.clear();

	if ( RayBoxIntersection( ray, bounds_, t0, t1 ) )
	{
		const Vector3 p = ray.eval( MIN( t0, t1 ) );

		ray.origin = p; // posuneme paprsek k objemu
		ray.t = REAL_MAX;

		CellIndices actual_cell_indices = cell_indices( p ); // indexy první zasažené buňky

		float t_0 = 0; // předpokládáme, že p leží na mezích objemu, první t_0 musí být tedy 0
		float t_1 = 0; // t_1 neumíme zatím určit, takže taky 0		

		const CellIndices next_cell_indices = CellIndices( // změna indexu při průchodu paprsku mezí buňky
			( ray.direction.x < 0 ) ? -1 : 1,
			( ray.direction.y < 0 ) ? -1 : 1,
			( ray.direction.z < 0 ) ? -1 : 1 );

		const Vector3 delta = ( cell_size_ / ray.direction ).Abs();	// parametrické vzdálenosti dvou následujících průsečíků vertikálních, resp. horizontálních, mezí buněk		

		Vector3 t_max = Vector3( // parametrická souřadnice prvního průsečíku s vertikální, resp. horizontální mezí objemu
			( bounds_[( ray.direction.x < 0 ) ? 1 : 0].x - p.x ) / ray.direction.x,
			( bounds_[( ray.direction.y < 0 ) ? 1 : 0].y - p.y ) / ray.direction.y,
			( bounds_[( ray.direction.z < 0 ) ? 1 : 0].z - p.z ) / ray.direction.z );

		/*t_max += delta * Vector3( // parametrická souřadnice prvního vnitřního průsečíku s vertikální, resp. horizontální mezí buněk
			( ray.direction.x < 0 ) ? n_ - 1 - actual_cell_indices.i : actual_cell_indices.i,
			( ray.direction.y < 0 ) ? width_ - 1 - actual_cell_indices.j : actual_cell_indices.j,
			( ray.direction.z < 0 ) ? height_ - 1 - actual_cell_indices.k : actual_cell_indices.k );*/ // !!! CHYBA !!!
		t_max += delta * Vector3( // parametrická souřadnice prvního vnitřního průsečíku s vertikální, resp. horizontální mezí buněk
			( ray.direction.x < 0 ) ? n_ - 1 - actual_cell_indices.i : actual_cell_indices.i + 1,
			( ray.direction.y < 0 ) ? width_ - 1 - actual_cell_indices.j : actual_cell_indices.j + 1,
			( ray.direction.z < 0 ) ? height_ - 1 - actual_cell_indices.k : actual_cell_indices.k + 1 ); // !!! OPRAVENO přidáním +1 !!!

		bool is_inside = true; // příznak, že paprsek je stále uvnitř objemu

		while ( is_inside )
		{
			t_0 = t_1;

			switch ( t_max.SmallestComponent( true ) )
			{
			case 0: // tx je minimální
				t_1 = t_max.x;
				t_max.x += delta.x;
				//printf( "<%0.3f, %0.3f> in [%d, %d, %d]\n", t_0, t_1, actual_cell_indices.i, actual_cell_indices.j, actual_cell_indices.k ); // tady známe aktuální interval <t_0, t_1> nad (i, j, k)-tou buňkou
				if ( t_0 < t_1 ) traversed_cells.push_back( CellHit( actual_cell_indices, t_0, t_1 ) );
				actual_cell_indices.i += next_cell_indices.i; // protnuli jsme levou, resp. pravou, stěnu buňky, takže pokračujeme levou, resp. pravou, buňkou
				if ( ( actual_cell_indices.i < 0 ) || ( actual_cell_indices.i >= n_ - 1 ) ) is_inside = false; // nebo is_inside &= ( ( i >= 0 ) && ( i < volume_i ) );
				break;

			case 1: // ty je minimální
				t_1 = t_max.y;
				t_max.y += delta.y;
				//printf( "<%0.3f, %0.3f> in [%d, %d, %d]\n", t_0, t_1, actual_cell_indices.i, actual_cell_indices.j, actual_cell_indices.k ); // tady známe aktuální interval <t_0, t_1> nad (i, j, k)-tou buňkou
				if ( t_0 < t_1 ) traversed_cells.push_back( CellHit( actual_cell_indices, t_0, t_1 ) );
				actual_cell_indices.j += next_cell_indices.j;
				if ( ( actual_cell_indices.j < 0 ) || ( actual_cell_indices.j >= width_ - 1 ) ) is_inside = false;
				break;

			case 2: // tz je minimální
				t_1 = t_max.z;
				t_max.z += delta.z;
				//printf( "<%0.3f, %0.3f> in [%d, %d, %d]\n", t_0, t_1, actual_cell_indices.i, actual_cell_indices.j, actual_cell_indices.k ); // tady známe aktuální interval <t_0, t_1> nad (i, j, k)-tou buňkou
				if ( t_0 < t_1 ) traversed_cells.push_back( CellHit( actual_cell_indices, t_0, t_1 ) );
				actual_cell_indices.k += next_cell_indices.k;
				if ( ( actual_cell_indices.k < 0 ) || ( actual_cell_indices.k >= height_ - 1 ) ) is_inside = false;
				break;
			}
		}
	}
	else
	{
		// nic, paprsek jde mimo objem
	}
}

// [i, j, k] je index buňky a chci ziskat offset do dat pro A
int Volume::offset( const int i, const int j, const int k ) const
{
	assert( ( i >= 0 ) && ( i < n_ ) );
	assert( ( j >= 0 ) && ( j < width_ ) );
	assert( ( k >= 0 ) && ( k < height_ ) );

	return ( width_step_ * height_ * i ) + ( j )+( ( height_ - 1 - k ) * width_step_ );
}

void Volume::Raycast( Camera & camera, const int samples )
{
	printf( "Start ray casting...\n" );

	cv::namedWindow( "Render", cv::WINDOW_AUTOSIZE );
	cv::moveWindow( "Render", 0, 0 );
	cv::Mat image = cv::Mat( cv::Size( camera.width(), camera.height() ),
		CV_32FC3, cv::Scalar( 0, 0, 0 ) );
	cv::imshow( "Render", image );
	cv::waitKey( 50 );

	const int no_threads = omp_get_max_threads();
	omp_set_num_threads( no_threads );
	std::vector<CellHit> * traversed = new std::vector<CellHit>[no_threads];

	const float ds = 1 / static_cast< float >( samples ); // jittered sampling

	const double t0 = omp_get_wtime();
	const Vector3 light(0, 0, 300.0f);
	int y;
	int no_rays = 0;

#pragma omp parallel for schedule( dynamic, 50 ) default( none ) private( y ) shared( camera, no_rays, traversed, image )
	for ( y = 0; y < camera.height(); ++y )
	{
		const int tid = omp_get_thread_num();

		const float y0 = y + ( ds - 1 ) * static_cast< float >( 0.5 );

		for ( int x = 0; x < camera.width(); ++x )
		{
			const float x0 = x + ( ds - 1 ) * static_cast< float >( 0.5 );

			Vector3 color;
			float value = 0;
			int color_samples = 0;

			for ( int ym = 0; ym < samples; ++ym )
			{
				for ( int xm = 0; xm < samples; ++xm )
				{
					const float ksi1 = ( ( samples > 1 ) ? Random() : static_cast< float >( 0.5 ) );
					const float ksi2 = ( ( samples > 1 ) ? Random() : static_cast< float >( 0.5 ) );

					float cx = x0 + ( ( ksi1 - static_cast< float >( 0.5 ) ) + xm ) * ds;
					float cy = y0 + ( ( ksi2 - static_cast< float >( 0.5 ) ) + ym ) * ds;

					Ray ray = camera.GenerateRay( cx, cy );
#pragma omp atomic
					++no_rays;

					if ( no_rays % 1000 == 0 )
					{
						printf( "\r%0.1f %%", ( 100.0 * no_rays ) / ( camera.width() * camera.height() * SQR( samples ) ) );
						cv::imshow( "Render", image );
						cv::waitKey( 10 );
					}

					Traverse( ray, traversed[tid] );

					for ( int i = 0; i < traversed[tid].size(); ++i )
					{
						auto hit = traversed[tid][i];
						Cell actual_cell = cell(hit.indices);
						auto t = actual_cell.find_iso_surface(ray, hit.t_0, hit.t_1, 0.25f);

						// Normal
						Vector3 n = actual_cell.grad_gramma(ray, t);
						float dot = n.DotProduct(Vector3(0, 0, 1.0f));

						if(t > 0)
						{
							value = MAX(0, dot);
							break;
						}
						//value += cell_hit.f;

						//printf( "%0.3f : <%0.3f, %0.3f> in [%d, %d, %d]\n", cell_hit.f, cell_hit.t_0, cell_hit.t_1, cell_hit.indices.i, cell_hit.indices.j, cell_hit.indices.k );
					}
				}
			}

			value *= SQR( ds );
			//value *= 1e-2f;
			image.at<cv::Vec3f>( y, x ) = cv::Vec3f( value, value, value );
		}
	}

	const double t1 = omp_get_wtime();

	printf( "\r%0.2f Mrays/sec\n", ( no_rays / ( t1 - t0 ) ) * 1e-6 );

	cv::imshow( "Render", image );
	cv::imwrite( "output.png", image * 255 );
	cv::waitKey( 0 );

	cv::destroyAllWindows();
}