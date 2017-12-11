#pragma once
#include <genLayer.h>
#include <block.h>
#include <biome.h>
#include <chunk.h>
#include <algorithm>
#include <noise.h>

class ChunkGeneratorOverWorldGrain
{
private:
	vector<vector<vector<float>>> _densityMap;
	vector<vector<vector<float>>> _depthMap;
	vector<vector<vector<float>>> _mainNoiseMap;
	vector<vector<vector<float>>> _minLimitMap;
	vector<vector<vector<float>>> _maxLimitMap;
	vector<vector<vector<float>>> _surfaceMap;

	OctavedNoise _depthNoise;
	OctavedNoise _mainNoise;
	OctavedNoise _maxNoise;
	OctavedNoise _minNoise;
	OctavedNoise _surfaceNoise;

	int _seed;
	//Random _random;

	vector<vector<float>> _biomeWeights;

	vector<vector<Biome>> _biomesForGeneration; // 10x10 or 16 x 16

	GenLayer *_genlayer;

public:
	ChunkGeneratorOverWorldGrain()
		: _depthNoise(OctavedNoise(PerlinNoise(0), 8, 0.5F)), _mainNoise(OctavedNoise(PerlinNoise(0), 8, 0.5F)),
		_maxNoise(OctavedNoise(PerlinNoise(0), 8, 0.5F)), _minNoise(OctavedNoise(PerlinNoise(0), 8, 0.5F)),
		_surfaceNoise(OctavedNoise(PerlinNoise(0), 8, 0.5F))
	{
		_densityMap = vector<vector<vector<float>>>(5, vector<vector<float>>(33, vector<float>(5, 0)));
		_depthMap = vector<vector<vector<float>>>(5, vector<vector<float>>(1, vector<float>(5, 0)));
		_mainNoiseMap = vector<vector<vector<float>>>(5, vector<vector<float>>(33, vector<float>(5, 0)));
		_minLimitMap = vector<vector<vector<float>>>(5, vector<vector<float>>(33, vector<float>(5, 0)));
		_maxLimitMap = vector<vector<vector<float>>>(5, vector<vector<float>>(33, vector<float>(5, 0)));
		_surfaceMap = vector<vector<vector<float>>>(16, vector<vector<float>>(1, vector<float>(16, 0)));

		
		_seed = 6748;
		srand(_seed);
		//_random = new Random(_seed);
		int a = rand();
		_depthNoise = OctavedNoise(PerlinNoise(a), 8, 0.5F);
		_mainNoise = OctavedNoise(PerlinNoise(rand()), 8, 0.5F);
		_maxNoise = OctavedNoise(PerlinNoise(rand()), 8, 0.5F);
		_minNoise = OctavedNoise(PerlinNoise(rand()), 8, 0.5F);
		_surfaceNoise = OctavedNoise(PerlinNoise(rand()), 8, 0.5F);

		_biomeWeights = vector<vector<float>>(5, vector<float>(5, 0));
		for (int i = -2; i <= 2; ++i)
		{
			for (int j = -2; j <= 2; ++j)
			{
				float f = 10.0F / (float)sqrt((i * i + j * j) + 0.2);
				_biomeWeights[i + 2][j + 2] = f;
			}
		}

		_biomesForGeneration = vector<vector<Biome>>(16, vector<Biome>(16, Biome()));

		_genlayer = GenLayer().InitAllLayer(_seed);
		//return Task.CompletedTask;
	}
	
	ChunkColumnStorage Generate(/*IWorld world, */int x, int z, GeneratorSettings settings)
	{
		ChunkColumnStorage chunkColumn;
		for (int i = 0; i < 16; ++i)
			chunkColumn.Sections[i] = ChunkSectionStorage(true);

		/*auto info = new MapGenerationInfo
		{
			Seed = await world.GetSeed()
		};*/
		GenerateChunk(/*info, */chunkColumn, x, z, settings);
		/*PopulateChunk(world, chunkColumn, x, z, settings);*/
		/*return chunkColumn.Compact();*/
		return chunkColumn;
	}

private:
	void GenerateChunk(/*MapGenerationInfo info, */ChunkColumnStorage &chunk, int x, int z, GeneratorSettings settings)
	{
		// ����Ⱥϵ����
		// ��ȡ����Ⱥϵ
		vector<vector<int>> biomeIds = _genlayer->GetInts(x * 16 - 8, z * 16 - 8, 32, 32);

		for (int i = 0; i < 10; ++i)
		{
			for (int j = 0; j < 10; ++j)
			{
				_biomesForGeneration[j][i] = GetBiome(biomeIds[(int)(0.861111F * j * 4)][(int)(0.861111F * i * 4)], settings);
			}
		}

		// ������������
		GenerateBasicTerrain(chunk, x, z, settings);

		// ��ȡ����Ⱥϵ
		biomeIds = _genlayer->GetInts(x * 16, z * 16, 16, 16);

		for (int i = 0; i < 16; ++i)
		{
			for (int j = 0; j < 16; ++j)
			{
				_biomesForGeneration[j][i] = GetBiome(biomeIds[j][i], settings);
			}
		}

		// ��������Ⱥϵ
		for (int i = 0; i < 16; ++i)
		{
			for (int j = 0; j < 16; ++j)
			{
				chunk.Biomes[j * 16 + i] = (int)_biomesForGeneration[j][i].GetBiomeId();
			}
		}

		// �������Ⱥϵ���з���
		ReplaceBiomeBlocks(settings, x, z, chunk, _biomesForGeneration);

		// Todo genrate structure
		// ���ɶ�Ѩ
		/*if (settings.UseCaves)
		{
			CavesGenerator generator = new CavesGenerator(info);
			generator.Generate(info, x, z, chunk, _biomesForGeneration[8, 8]);
		}*/

		//// ����skylight
		//GenerateSkylightMap(chunk);
	}

//public:
//	void PopulateChunk(IWorld world, ChunkColumnStorage &chunk, int x, int z, GeneratorSettings settings)
//	{
//		int blockX = x * 16;
//		int blockZ = z * 16;
//		Biome chunkBiome = Biome.GetBiome(chunk.Biomes[7 * 16 + 7], settings);
//
//		chunkBiome.Decorate(world, GrainFactory, chunk, _random, new BlockWorldPos{ X = blockX, Y = 0, Z = blockZ });
//		chunkBiome.SpawnMob(world, GrainFactory, chunk, _random, new BlockWorldPos{ X = blockX, Y = 0, Z = blockZ });
//	}
	template<class T>
	constexpr const T& clamp(const T& v, const T& lo, const T& hi)
	{
		return clamp(v, lo, hi, std::less<>());
	}
	template<class T, class Compare>
	constexpr const T& clamp(const T& v, const T& lo, const T& hi, Compare comp)
	{
		return assert(!comp(hi, lo)),
			comp(v, lo) ? lo : comp(hi, v) ? hi : v;
	}
private:
	void GenerateBasicTerrain(ChunkColumnStorage &chunk, int x, int z, GeneratorSettings settings)
	{
		// �����߶�ͼ
		GenerateDensityMap(_densityMap, x * 4, 0, z * 4, settings);

		// �������Բ�ֵ
		for (int xHigh = 0; xHigh < 4; ++xHigh)
		{
			for (int zHigh = 0; zHigh < 4; ++zHigh)
			{
				for (int yHigh = 0; yHigh < 32; ++yHigh)
				{
					double yPart111 = _densityMap[xHigh][yHigh][zHigh];
					double yPart121 = _densityMap[xHigh][yHigh][zHigh + 1];
					double yPart211 = _densityMap[xHigh + 1][yHigh][zHigh];
					double yPart221 = _densityMap[xHigh + 1][yHigh][zHigh + 1];
					double yDensityStep11 = (_densityMap[xHigh][yHigh + 1][zHigh] - yPart111) * 0.125;
					double yDensityStep12 = (_densityMap[xHigh][yHigh + 1][zHigh + 1] - yPart121) * 0.125;
					double yDensityStep21 = (_densityMap[xHigh + 1][yHigh + 1][zHigh] - yPart211) * 0.125;
					double yDensityStep22 = (_densityMap[xHigh + 1][yHigh + 1][zHigh + 1] - yPart221) * 0.125;

					for (int yLow = 0; yLow < 8; ++yLow)
					{
						double density111 = yPart111;
						double density121 = yPart121;
						double xDensityStep11 = (yPart211 - yPart111) * 0.25;
						double xDensityStep21 = (yPart221 - yPart121) * 0.25;

						for (int xLow = 0; xLow < 4; ++xLow)
						{
							double zDensityStep11 = (density121 - density111) * 0.25;
							double blockValue = density111 - zDensityStep11;

							for (int zLow = 0; zLow < 4; ++zLow)
							{
								int posX = xHigh * 4 + xLow;
								int posY = yHigh * 8 + yLow;
								int posZ = zHigh * 4 + zLow;
								blockValue += zDensityStep11;
								if (blockValue-190 > 0.0)
								{
									chunk(posX, posY, posZ) = BlockState(BlockId::Stone, StoneType::Stone);
								}
								else if (posY < settings.SeaLevel)
								{
									chunk(posX, posY, posZ) = BlockState(BlockId::Water, FluidType::FallingFlag);
								}
								else
								{
									chunk(posX, posY, posZ) = BlockState(BlockId::Air, 0);
								}
							}

							density111 += xDensityStep11;
							density121 += xDensityStep21;
						}

						yPart111 += yDensityStep11;
						yPart121 += yDensityStep12;
						yPart211 += yDensityStep21;
						yPart221 += yDensityStep22;
					}
				}
			}
		}
	}

private:
	void GenerateDensityMap(vector<vector<vector<float>>> & densityMap, int xOffset, int yOffset, int zOffset, GeneratorSettings settings)
	{
		_depthNoise.Noise(
			_depthMap,
			glm::vec3(xOffset + 0.1f, 0.0f, zOffset + 0.1f),
			glm::vec3(settings.DepthNoiseScaleX, 1.0f, settings.DepthNoiseScaleZ));

		float coordinateScale = settings.CoordinateScale;
		float heightScale = settings.HeightScale;

		// ����3��5*5*33������
		_mainNoise.Noise(
			_mainNoiseMap,
			glm::vec3(xOffset, yOffset, zOffset),
			glm::vec3(
				coordinateScale / settings.MainNoiseScaleX,
				heightScale / settings.MainNoiseScaleY,
				coordinateScale / settings.MainNoiseScaleZ));

		_minNoise.Noise(
			_minLimitMap,
			glm::vec3(xOffset, yOffset, zOffset),
			glm::vec3(
				coordinateScale,
				heightScale,
				coordinateScale));

		_maxNoise.Noise(
			_maxLimitMap,
			glm::vec3(xOffset, yOffset, zOffset),
			glm::vec3(
				coordinateScale,
				heightScale,
				coordinateScale));

		// chunk����
		for (int x1 = 0; x1 < 5; ++x1)
		{
			for (int z1 = 0; z1 < 5; ++z1)
			{
				float scale = 0.0F;
				float groundYOffset = 0.0F;
				float totalWeight = 0.0F;

				// ���ĵ�����Ⱥϵ
				Biome centerBiome = _biomesForGeneration[z1 + 2][x1 + 2];

				// ��scale��groundYOffset�ļ�Ȩƽ��ֵ
				for (int x2 = 0; x2 < 5; ++x2)
				{
					for (int z2 = 0; z2 < 5; ++z2)
					{
						Biome biome = _biomesForGeneration[z1 + z2][x1 + x2];
						float curGroundYOffset = settings.BiomeDepthOffSet + biome.GetBaseHeight() * settings.BiomeDepthWeight; // biomeDepthOffSet=0
						float curScale = settings.BiomeScaleOffset + biome.GetHeightVariation() * settings.BiomeScaleWeight; // biomeScaleOffset=0

																															 // parabolicFieldΪ 10 / ��(�õ㵽���ĵ�ľ���^2 + 0.2)
						float weight = _biomeWeights[z2][x2] / (curGroundYOffset + 2.0F);

						if (biome.GetBaseHeight() > centerBiome.GetBaseHeight())
						{
							weight /= 2.0F;
						}

						scale += curScale * weight;
						groundYOffset += curGroundYOffset * weight;
						totalWeight += weight;
					}
				}

				scale = scale / totalWeight;
				groundYOffset = groundYOffset / totalWeight;
				scale = scale * 0.9F + 0.1F;
				groundYOffset = (groundYOffset * 4.0F - 1.0F) / 8.0F;

				// ȡһ��-0.36~0.125���������������������������ĵر�
				float random = (_depthMap[x1][0][z1] - 0.5F) * 2 / 8000.0F;
				if (random < 0.0F)
				{
					random = -random * 0.3F;
				}

				random = random * 3.0F - 2.0F;

				if (random < 0.0)
				{
					random = random / 2.0F;
					if (random < -1.0)
					{
						random = -1.0F;
					}

					random = random / 1.4F;
					random = random / 2.0F;
				}
				else
				{
					if (random > 1.0F)
					{
						random = 1.0F;
					}

					random = random / 8.0F;
				}

				float groundYOffset1 = groundYOffset;
				float scale1 = scale;

				// groundYOffset��-0.072~0.025�ı䶯��
				groundYOffset1 = groundYOffset1 + random * 0.2F;
				groundYOffset1 = groundYOffset1 * settings.BaseSize / 8.0F;

				// ����Ǵ�ŵĵ���y����
				float groundY = settings.BaseSize + groundYOffset1 * 4.0F; // baseSize=8.5��Ӧ�ô�����ƽ���ر�߶�68

																		   // ע�����y*8�������յ�y����
				for (int y = 0; y < 33; ++y)
				{
					// resultƫ����������Ǹ�����������壬������������Һ��Ϳ���
					float offset = (y - groundY) * settings.StretchY * 128.0F / 256.0F / scale1; // scale�����0.1~0.2����...

					if (offset < 0.0F)
					{
						offset *= 4.0F;
					}

					// ������֤lowerLimit < upperLimit������û��Ӱ��
					float lowerLimit = (_minLimitMap[x1][y][z1] - 0.5F) * 160000 / settings.LowerLimitScale; // lowerLimitScale=512
					float upperLimit = (_maxLimitMap[x1][y][z1] - 0.5F) * 160000 / settings.UpperLimitScale; // upperLimitScale=512
					float t = ((_mainNoiseMap[x1][y][z1] - 0.5F) * 160000 / 10.0F + 1.0F) / 2.0F;

					// �������t < 0��ȡlowerLimit��t > 1��ȡupperLimit��������tΪ�����������޼����Բ�ֵ
					float result;// = MathHelper.DenormalizeClamp(lowerLimit, upperLimit, t) - offset;

					if (t < 0)
					{
						result = lowerLimit - offset;
					}
					else if (t > 1)
					{
						result = upperLimit - offset;
					}
					else
					{
						result = lowerLimit + (upperLimit - lowerLimit) * t - offset;
					}

					// y = 30~32
					if (y > 29)
					{
						// ��ԭresult��-10֮�����Բ�ֵ������y > 240�ķ���ͻ�Խ��Խ�٣����ȫ��ɿ���
						float t2 = (float)(y - 29) / 3.0F;
						result = result * (1.0F - t2) + -10.0F * t2;
					}

					_densityMap[x1][y][z1] = (float)result;
				}
			}
		}

		densityMap = _densityMap;
	}

	void ReplaceBiomeBlocks(GeneratorSettings settings, int x, int z, ChunkColumnStorage &chunk, vector<vector<Biome>> biomesIn)
	{
		_surfaceNoise.Noise(
			_surfaceMap,
			glm::vec3(x * 16 + 0.1F, 0, z * 16 + 0.1F),
			glm::vec3(0.0625F, 1.0F, 0.0625F));
		srand(_seed);
		for (int x1 = 0; x1 < 16; ++x1)
		{
			for (int z1 = 0; z1 < 16; ++z1)
			{
				Biome biome = biomesIn[z1][x1];
				biome.GenerateBiomeTerrain(settings.SeaLevel, _seed, chunk, x, z, x1, z1, (_surfaceMap[x1][0][z1] - 0.5) * 2);
			}
		}
	}

	/*private void GenerateSkylightMap(ChunkColumnStorage &chunk)
	{
		for (int i = 0; i < ChunkConstants.SectionsPerChunk; ++i)
		{
			auto skyLight = chunk.Sections[i].SkyLight;
			for (int y = 0; y < ChunkConstants.BlockEdgeWidthInSection; y++)
			{
				for (int z = 0; z < ChunkConstants.BlockEdgeWidthInSection; z++)
				{
					for (int x = 0; x < ChunkConstants.BlockEdgeWidthInSection; x++)
					{
						skyLight[x, y, z] = 0xF;
					}
				}
			}
		}
	}*/

	int GetDensityMapIndex(int x, int y, int z)
	{
		return (x * 5 + z) * 33 + y;
	}

	double GetDensityMapValue(vector<double> densityMap, int x, int y, int z)
	{
		return densityMap[(x * 5 + z) * 33 + y];
	}
};