#include <array>
#include <cstdint>
#include <cassert>
#include <variant>
#include <vector>

#include <H5DataType.h>
#include <H5PredType.h>

namespace H5Utils
{
	class VectorHolder // copied form PointData for now, and added double
	{
	private:
		using VariantOfVectors = std::variant <
			std::vector<float>,
			std::vector<biovault::bfloat16_t>,
			std::vector<std::int16_t>,
			std::vector<std::uint16_t>,
			std::vector<std::int8_t>,
			std::vector<std::uint8_t> >;

		typedef std::variant_alternative_t<0, VariantOfVectors> fallback_type;
		VariantOfVectors _variantOfVectors;
	public:
		enum class ElementTypeSpecifier
		{
			float32,
			bfloat16,
			int16,
			uint16,
			int8,
			uint8,

			fallback = float32
		};

		

		static constexpr std::array<const char*, std::variant_size_v<VariantOfVectors>> getElementTypeNames()
		{
			return
			{ {
				"float32",
				"bfloat16",
				"int16",
				"uint16",
				"int8",
				"uint8"
			} };
		}

	private:

		

		// Sets the index of the specified variant. If the new index is different from the previous one, the value will be reset. 
	// Inspired by `expand_type` from kmbeutel at
	// https://www.reddit.com/r/cpp/comments/f8cbzs/creating_stdvariant_based_on_index_at_runtime/?rdt=52905
		template <typename... Alternatives>
		static void setIndexOfVariant(std::variant<Alternatives...> &var, std::size_t index)
		{
			if (index != var.index())
			{
				assert(index < sizeof...(Alternatives));
				const std::variant<Alternatives...> variants[] = { Alternatives{ }... };
				var = variants[index];
			}
		}


		// Returns the index of the specified alternative: the position of the alternative type withing the variant.
		// Inspired by `variant_index` from Bargor at
		// https://stackoverflow.com/questions/52303316/get-index-by-type-in-stdvariant
		template<typename Variant, typename Alternative, std::size_t index = 0>
		static constexpr std::size_t getIndexOfVariantAlternative()
		{
			static_assert (index < std::variant_size_v<Variant>);

			if constexpr (std::is_same_v<std::variant_alternative_t<index, Variant>, Alternative>)
			{
				return index;
			}
			else
			{
				return getIndexOfVariantAlternative<Variant, Alternative, index + 1>();
			}
		}

	public:		

		template <typename T>
		const std::vector<T>& getConstVector() const
		{
			// This function should only be used to access the currently selected vector.
			assert(std::holds_alternative<std::vector<T>>(_variantOfVectors));
			return std::get<std::vector<T>>(_variantOfVectors);
		}

		template <typename T>
		const std::vector<T>& getVector() const
		{
			return getConstVector<T>();
		}

		template <typename T>
		std::vector<T>& getVector()
		{
			return const_cast<std::vector<T>&>(getConstVector<T>());
		}
	

		VectorHolder() = default;

		template <typename T>
		explicit VectorHolder(const std::vector<T>& vec)
		{
			auto* supported_vector = std::get_if<std::vector<T>>(_variantOfVectors);
			if (supported_vector)
			{
				*supported_vector = vec;
			}
			else
			{
                if (std::holds_alternative<fallback_type>(_variantOfVectors)) {
                    auto* fallback_vector = std::get<fallback_type>(_variantOfVectors); 
					//copy the values
					fallback_vector->assign(vec.cbegin(), vec.cend());
                }
				{
					//throw bad_variant_access
					std::get<std::vector<T>>(_variantOfVectors); 
				}
			}
		}


		/// Explicit constructor that efficiently "moves" the specified vector
		/// into the internal data structure. Ensures that the element type
		/// specifier matches the value type of the vector.
		template <typename T>
		explicit VectorHolder(std::vector<T>&& vec)
		{
			std::get<std::vector<T>>(_variantOfVectors) = std::move(vec);
		}


		/// Just forwarding to the corresponding member function of the currently selected std::vector.
		std::size_t size() const
		{
			return std::visit([](const auto& vec) { return vec.size(); }, _variantOfVectors);
		}

		/// Just forwarding to the corresponding member function of the currently selected std::vector.
		void resize(const std::size_t newSize)
		{
			return std::visit([newSize](auto& vec) { return vec.resize(newSize); }, _variantOfVectors);
		}

		/// Just forwarding to the corresponding member function of the currently selected std::vector.
		void clear()
		{
			return std::visit([](auto& vec) { return vec.clear(); }, _variantOfVectors);
		}

		/// Just forwarding to the corresponding member function of the currently selected std::vector.
		void shrink_to_fit()
		{
			return std::visit([](auto& vec) { return vec.shrink_to_fit(); }, _variantOfVectors);
		}

		void setElementTypeSpecifier(const ElementTypeSpecifier elementTypeSpecifier)
		{
			setIndexOfVariant(_variantOfVectors, static_cast<std::size_t>(elementTypeSpecifier));
		}

		ElementTypeSpecifier getElementTypeSpecifier() const
		{
			return static_cast<ElementTypeSpecifier>(_variantOfVectors.index());
		}

		template <typename T>
		static constexpr ElementTypeSpecifier getElementTypeSpecifier()
		{
			constexpr auto index = getIndexOfVariantAlternative<VariantOfVectors, std::vector<T>>();
			return static_cast<ElementTypeSpecifier>(index);
		}

		/// Resizes the currently active internal data vector to the specified
		/// number of elements, and converts the elements of the specified data
		/// to the internal data element type, by static_cast. 
		template <typename T>
		void convertData(const T* const data, const std::size_t numberOfElements)
		{
			std::visit([data, numberOfElements](auto& vec)
				{
					vec.resize(numberOfElements);

					std::size_t i{};
					for (auto& elem : vec)
					{
						elem = static_cast<std::remove_reference_t<decltype(elem)>>(data[i]);
						++i;
					}
				},
				_variantOfVectors);
		}

		// Similar to C++17 std::visit.
		template <typename ReturnType = void, typename FunctionObject>
		ReturnType constVisitFromBeginToEnd(FunctionObject functionObject) const
		{
			return std::visit([functionObject](const auto& vec) -> ReturnType
				{
					return functionObject(std::cbegin(vec), std::cend(vec));
				},
				_variantOfVectors);
		}

		// Similar to C++17 std::visit.
		template <typename ReturnType = void, typename FunctionObject>
		ReturnType visitFromBeginToEnd(FunctionObject functionObject)
		{
			return std::visit([functionObject](auto& vec) -> ReturnType
				{
					return functionObject(std::begin(vec), std::end(vec));
				},
				_variantOfVectors);
		}

		


		// Similar to C++17 std::visit.
		template <typename ReturnType = void, typename FunctionObject>
		ReturnType visit(FunctionObject functionObject)
		{
			return std::visit([functionObject](auto& vec) -> ReturnType
				{
					return functionObject(vec);
				},
				_variantOfVectors);
		}

		H5::DataType H5DataType() const
		{
			switch(static_cast<ElementTypeSpecifier>(_variantOfVectors.index()))
			{
				case ElementTypeSpecifier::float32: return H5::PredType::NATIVE_FLOAT;
				//case ElementTypeSpecifier::bfloat16: throw std::bad_variant_access();
				case ElementTypeSpecifier::int16: return H5::PredType::NATIVE_INT16;
				case ElementTypeSpecifier::uint16: return H5::PredType::NATIVE_UINT16;
				case ElementTypeSpecifier::int8: return H5::PredType::NATIVE_INT8;
				case ElementTypeSpecifier::uint8: return H5::PredType::NATIVE_UINT8;
				default: throw std::bad_variant_access();
			}
		}

		
		void setPredTypeSpecifier(const H5::PredType &predType)
		{
			if (predType == H5::PredType::NATIVE_UINT8)
				setElementTypeSpecifier(ElementTypeSpecifier::uint8);
			else if (predType == H5::PredType::NATIVE_UINT16)
				setElementTypeSpecifier(ElementTypeSpecifier::uint16);
			else if (predType == H5::PredType::NATIVE_INT8)
				setElementTypeSpecifier(ElementTypeSpecifier::int8);
			else if (predType == H5::PredType::NATIVE_INT16)
				setElementTypeSpecifier(ElementTypeSpecifier::int16);
			else if (predType == H5::PredType::NATIVE_FLOAT)
				setElementTypeSpecifier(ElementTypeSpecifier::float32);
			else
				setElementTypeSpecifier(ElementTypeSpecifier::fallback);
		}

		void* data()
		{
			return std::visit([](auto& vec) { return static_cast<void*>( vec.data()); }, _variantOfVectors);
		}
	};
}
